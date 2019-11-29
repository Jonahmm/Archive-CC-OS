#include "fs.h"

bool read_full_block(fs_meta* meta, blk_id block, block_t* out) {
    return disk_rd(block + meta->blk_0, (uint8_t*) out, BLOCK_SIZE) >= 0;
}

bool write_full_block(fs_meta* meta, blk_id block, block_t* in) {
    return disk_wr(block + meta->blk_0, (uint8_t*) in, BLOCK_SIZE) >= 0;
}

bool load_hblock(fs_meta* meta) {
    //Load block 0 into meta's header 
    return read_full_block(meta, 0, (block_t*) &meta->header);
}

bool save_hblock(fs_meta* meta) {
    return write_full_block(meta, 0, (block_t*) &meta->header);
}

// Return passthrough so can be called like 'return set_result(…,r)'
int set_result(fs_meta* meta, fs_msg_t msg, int return_val) {
    meta->result = msg;
    return return_val;
}

// Load volume metadata into the given meta structure
bool load_volume(fs_meta* meta) {
    // Load header block
    if (!load_hblock(meta)) return false;
    // hblock was read and is now stored in meta->header
    if (strncmp("CWFS 1.0", meta->header.ident, 8) != 0) return false;
    // identifier was as expected for a CWFS volume, 
    // Volume data has now been loaded into meta and can be used
    return true;
}

//Initialise a new directory with no children
void new_dir_pl(fs_pl_dir * dir) {
    for (int i = 0; i < MAX_CHILDREN; i++) dir->children[i] = UNUSED_VALUE;
}

// Return 0 if no valid region, first block of region otherwise
blk_id find_free_region(fs_meta* meta, uint8_t rsize) {
    // In this first version, just allocate a region at the end of used space
    blk_id new = meta->header.next_blk;
    blk_id next = new + rsize;
    if (next <= meta->header.nblocks) meta->header.next_blk = next;
    else return set_result(meta, DISK_FULL, 0);
    return set_result(meta, SUCCESS, new);
}

//Allocate region at index given
blk_id new_iregion(fs_meta* meta, int ireg_index) {
    blk_id start = find_free_region(meta, meta->header.ireg_ds);
    if (meta->result != SUCCESS) return 0; //FAIL: disk full
    //Allocate the iregion
    meta->header.ireg_start[ireg_index] = start;
    meta->header.ireg_size [ireg_index] = meta->header.ireg_ds;
    //////////////////////
    // SAVE HBLOCK HERE //
    //////////////////////
    if (!save_hblock(meta)) return set_result(meta, WRITE_ERROR, 0); //FAIL: Disk write error
    return set_result(meta, SUCCESS, start);
    // Save hblock!
}

// Returns 0 if id is out of range, otherwise returns the block address of the
// iblock specified. Will allocate a new iregion if possible
blk_id iblock_index_to_disk_index(fs_meta* meta, blk_id iblock_index) {
    blk_id a = 0;
    int i = 0;
    while (meta->header.ireg_start[i] != UNUSED_VALUE
           && i < MAX_IREGIONS) {
        blk_id b = a + meta->header.ireg_size[i];
        if (iblock_index < b) {
            return meta->header.ireg_start[i] + iblock_index - a;
        }
        i++;
        a = b;

    }
    if (i == MAX_IREGIONS) return set_result(meta, HBLOCK_FULL, 0); //FAIL: index out of range
    // This function was told to allocate a new iregion. Return its starting index
    // or 0 in case of FAIL: IO error
    return new_iregion(meta, i);
}

bool save_irecord(fs_meta* meta, irecord* irec, uint32_t iindex) {
    blk_id iblock_ind = (iindex / IRECORDS_PER_BLOCK);
    blk_id iblock_loc = iblock_index_to_disk_index(meta, iblock_ind);
    iblock_t iblock;
    if (!iblock_loc) return false; // ← FAIL: ?     /     FAIL: IO Error ↓
    if (!read_full_block(meta, iblock_loc, (block_t*) &iblock))
        return set_result(meta, READ_ERROR, false);
    memcpy(&iblock.records[(iindex % IRECORDS_PER_BLOCK)], &iblock, BLOCK_SIZE);
    if (!write_full_block(meta, iblock_loc, (block_t*) &iblock))
        return set_result(meta, WRITE_ERROR, false);
    return set_result(meta, SUCCESS, true); 
}

bool add_irecord(fs_meta* meta, irecord* irec) {
    uint32_t iindex = meta->header.next_irecord;
    if (save_irecord(meta, irec, iindex)) {
        meta->header.next_irecord++;
        return true;
    }
    return false;
}

bool create_root(fs_meta* meta) {
    irecord root;
    // the root's name and parent fields will never be queried, so ignore them
    root.permissions = meta->header.perms_d;
    root.type = FT_DIR;
    new_dir_pl((fs_pl_dir*) &root.payload);
    return add_irecord(meta, &root);
}

// Create a new volume at the blockid specified in meta, loading the rest of
// the details into meta->header
bool format_volume(fs_meta* meta, uint32_t nblocks,
                    uint8_t ireg_ds, uint8_t freg_ds, uint8_t perms_d) {
    // The minimum functional size (for a volume with only directory entries)
    // is two blocks.
    if (nblocks < 2) return false;
    // Init nblocks
    meta->header.nblocks = nblocks;
    // Identify the volume 
    strncpy(meta->header.ident, "CWFS 1.0", 8);
    // Set default values for iregion & fregion size, permissions
    meta->header.freg_ds = freg_ds;
    meta->header.ireg_ds = ireg_ds;
    meta->header.perms_d = perms_d;
    // Set default tracking values
    meta->header.next_blk = 1;
    meta->header.next_irecord = 0;
    // for (int i = 0; i < MAX_IREGIONS; i++) {
    //     meta->header.ireg_start[i] = UNUSED_VALUE;
    // }
    memset(&meta->header.ireg_start[0], UNUSED_VALUE, 4*MAX_IREGIONS);
    //Create the root directory. The header will be saved as part of this
    create_root(meta);
}

