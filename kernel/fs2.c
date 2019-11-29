/* Copyright (C) 2019 Jonah McPartlin
 *
 * Use of this source code is restricted per the CC BY-NC-ND license, a copy of 
 * which can be found via http://creativecommons.org (and should be included as 
 * LICENSE.txt within the associated archive or repository).
 */
#include "fs2.h"
#include "disk.h"
#include "string.h"

#define FS_DEBUG false

#if FS_DEBUG
#include "PL011.h"
#endif
void dpr(char* x) {
    #if FS_DEBUG
    PL011_putc(UART0, 'F', true);
    PL011_putc(UART0, ':', true);
    PL011_putc(UART0, ' ', true);
    while (*x != '\0') {
        PL011_putc(UART0, *x, true);
        x++;
    }
    PL011_putc(UART0, '\n', true);
    #endif
}

//////////////////////////////
//       LOAD / SAVE       //
////////////////////////////


bool fs2_rblk(fs2_volume_t* vol, uint32_t blkindex, uint8_t* out) {
    return disk_rd(vol->blk_0 + blkindex, out, FS2_BLOCK_SZ) >= 0;
}
bool fs2_wblk(fs2_volume_t* vol, uint32_t blkindex, uint8_t* in) {
    return disk_wr(vol->blk_0 + blkindex,  in, FS2_BLOCK_SZ) >= 0;
}

void fs2_save_iblock_c(fs2_volume_t* vol) {
    dpr("Saving cached iblock");
    vol->outcome = fs2_wblk(vol, vol->icaddr, (uint8_t*) &vol->icache)
                 ? FS2_SUCCESS
                 : FS2_DISK_WR_ERR;
}

uint32_t fs2_find_free_region(fs2_volume_t* vol, uint8_t rsz, bool save) {
    dpr("Looking for a free region");
    for (int i = vol->hblock.separator; i < 800; --i) {
        if (vol->hblock.reg_len[i] == rsz) {
            // This is likely as many directory and file regions are 1-long

        }
        // attempt to use a free region
    }
    uint32_t onb = vol->hblock.next_block;
    uint32_t nnb = onb + rsz;
    if (nnb > vol->hblock.nblocks) {
        vol->outcome = FS2_DISK_FULL;
        return 0;
    }
    vol->hblock.next_block = nnb;
    vol->outcome = save 
                 ?  fs2_wblk(vol, 0, (uint8_t*) &vol->hblock)
                    ? FS2_SUCCESS
                    : FS2_DISK_WR_ERR
                 : FS2_SUCCESS;
    return onb;
}

// Load the iblock at the given index into the given structure, if it exists. 
// Otherwise, attempt to allocate it and don't bother loading. This relies on
// the condition that 'ibindex' will only be <= (the index of the last alloc'd
// iblock + 1) so it need only allocate one extra region. Returns the index of
// the iblock for easy saving. The final flag is included so a method that is
// going to save the hblock anyway can avoid the overhead of the first save.
uint32_t fs2_get_iblock(fs2_volume_t* vol, fs2_iblock_t* iblock, uint32_t ibindex, bool save_hblock) {
    dpr("Looking for iblock…");
    int b; int n = 0; int i = 0;
    while (i < vol->hblock.separator && vol->hblock.reg_start[i] != 0) {
        // Iterate through all active blocks until either we find the block or
        // must allocate a new one
        b = n + vol->hblock.reg_len[i];
        // Is our iblock in the ith iregion?
        if (ibindex < b) {
            dpr("Block found. Loading…");
            // The block is allocated and lives at the below address
            uint32_t ibaddr = vol->hblock.reg_start[i]
                            + ibindex
                            - n;
            // Copy the block into memory
            vol->outcome    = (!fs2_rblk(vol, ibaddr, (uint8_t*) iblock))
                            ? FS2_DISK_RD_ERR
                            : FS2_SUCCESS;
            return ibaddr;
            dpr("Done");
        }
        //Set up next iteration
        n = b; i++;
    }
    if (i == vol->hblock.separator) {
        dpr("iblock not found and hblock is full => cannot allocate new iblock.");
        vol->outcome = FS2_HBLOCK_FULL;
        return 0;
    }
    uint32_t nreg_start = fs2_find_free_region(vol, vol->hblock.default_ireg_len, false);
    if (nreg_start == 0) return 0; // Error value will remain in vol
    // Free region was found, assign it in hblock and SAVE
    dpr("iblock was not found so a new one is allocated.");
    vol->hblock.reg_start[i] = nreg_start;
    vol->hblock.reg_len  [i] = vol->hblock.default_ireg_len;
    vol->outcome = save_hblock
                 ? ( (fs2_wblk(vol, 0, (uint8_t*) &vol->hblock))
                     ? FS2_SUCCESS 
                     : FS2_DISK_WR_ERR )
                 : FS2_SUCCESS;    
    return nreg_start;
}

// Cached wrapper for fs2_read_iblock, can save before loading if necessary.
void fs2_get_iblock_c(fs2_volume_t* vol, uint32_t ibindex, bool save_h, bool savechanges) {
    dpr("Loading iblock");
    if (ibindex != vol->icindex) {
        if (savechanges) {
            dpr("Saving changes to cache");
            fs2_save_iblock_c(vol);
            if (vol->outcome != FS2_SUCCESS) return;
        }
        dpr("Caching new iblock");
        vol->icindex = ibindex;
        vol->icaddr  = fs2_get_iblock(vol, &vol->icache, ibindex, save_h);
        if (vol->outcome != FS2_SUCCESS) return;
    } else dpr("Using cached iblock");
    vol->outcome = FS2_SUCCESS;
}

// If dry is set, just update the address.
void fs2_load_dblock_c(fs2_volume_t* vol, uint32_t daddr, bool dry) {
    dpr("Loading dblock");
    if (daddr != vol-> dcaddr) {
        dpr("Caching new dblock");
        if (!dry) // Separate if for clarity
          if (!fs2_rblk(vol, daddr, (uint8_t*) &vol->dcache))
            {vol->outcome = FS2_DISK_RD_ERR; return;}
        if (dry) dpr("(only updating address)");
        vol->dcaddr = daddr;
    } else dpr("Using cached dblock");
    vol->outcome = FS2_SUCCESS;
}

void fs2_save_dblock_c(fs2_volume_t* vol) {
    dpr("Saving cached dblock");
    vol->outcome = fs2_wblk(vol, vol->dcaddr, (uint8_t*) &vol->dcache)
                 ? FS2_SUCCESS
                 : FS2_DISK_WR_ERR;
}

void fs2_load_gp_c(fs2_volume_t* vol, uint32_t baddr, bool dry) {
    dpr("Loading general purpose block");
    if (baddr != vol-> xcaddr) {
        dpr("Caching new GP block");
        if (!dry) {
            if (!fs2_rblk(vol, baddr, (uint8_t*) &vol->xcache))
            {vol->outcome = FS2_DISK_RD_ERR; return;}
        } else dpr("(only updating address)");
        vol->xcaddr = baddr;
    } else dpr("Using cached block");
    vol->outcome = FS2_SUCCESS;
}

void fs2_save_gp_c(fs2_volume_t* vol) {
    dpr("Saving general purpose block");
    vol->outcome = fs2_wblk(vol, vol->xcaddr, (uint8_t*) &vol->xcache)
                 ? FS2_SUCCESS
                 : FS2_DISK_WR_ERR;
}

void fs2_load_volume(fs2_volume_t* vol) {
    dpr("Loading volume");
    //Read the block at the given address
    
    if (!fs2_rblk(vol, 0, (uint8_t*) &vol->hblock)) {
        vol->outcome = FS2_DISK_RD_ERR;
        return;
    }
    if (strncmp(vol->hblock.ident, "CWFS 2.1", 8) != 0) {
        vol->outcome = FS2_INVALID_FS;
        return;
    }
    vol->icindex = vol->dcaddr = vol->xcaddr = -1;
    vol->outcome = FS2_SUCCESS;
}

// Checks if the dir at the given iindex has a child with the name given.
// Recycles iblock, assuming that dinode is in the given iiblock
uint32_t fs2_find_in_dir_r(fs2_volume_t* vol, uint32_t diindex, 
            char* name, int nchars) {
    
    // if (ibindex != diindex / 32) fs2_get_iblock(vol, iblock, diindex / 32, false);
    fs2_get_iblock_c(vol, diindex / 32, false, false);
    if (vol->outcome != FS2_SUCCESS)  return -1;

    fs2_inode_t* dinode = &vol->icache.inodes[diindex % 32];
    if (dinode->ftype != FS2_FTYPE_DIR) {
        vol->outcome = FS2_BAD_FTYPE; return -1;
    }
    // fs2_dblock_t dblock;
    uint32_t k,i,j; k = 0; i = 0; j = 0;
    while (k < dinode->eof) {
        if ((k % 128) == 0) {
            // Load new dblock
            fs2_load_dblock_c(vol, dinode->reg_start[i] + j, false);
            if (vol->outcome != FS2_SUCCESS) return -1;
            j++;
            if (j == dinode->reg_len[i]) {
                i++; j = 0;
            }
            // Neither of these cases should happen if the while condition
            // was satisfied, but included for protection against corrupt inode
            if (i == 23 || dinode->reg_start[i] == 0) {
                vol->outcome = FS2_NO_FILE;     return -1;
            }
        }
        if (strncmp(vol->dcache.entries[k % 128].name, name, nchars) == 0) {
            vol->outcome = FS2_SUCCESS;
            return  vol->dcache.entries[k % 128].inode_index;
        }
        k++;
    }
    vol->outcome = FS2_NO_FILE;
    return -1;
}

// Return inode index of file/dir if it exists, -1 otherwise. If putparent is a
// valid address, the inode index of the parent will be saved to it.
uint32_t fs2_find_file(fs2_volume_t* vol, char* path, uint32_t* putparent) {
    dpr("Looking for file");
    if (*path == '\0') {
        vol->outcome = FS2_SUCCESS;
        return 0;
    }
    uint32_t parent = 0; uint32_t child;
    fs2_get_iblock_c(vol, 0, false, false);
    char* cursor = path;
    int i;
    dpr("Checking root");
    while(1) {
        // dpr(cursor);
        i = 0;
        while (cursor[i] != '\0' && cursor[i] != '/') i++;
        if (i > 28) {vol->outcome = FS2_INVALID_PATH; return -1;}
        child = fs2_find_in_dir_r(vol, parent, cursor, i);
        if (child == -1) {
            // Err value will pass through
            return -1;
        }
        if (cursor[i] == '\0') {
            vol->outcome = FS2_SUCCESS;
            if (putparent != NULL) *putparent = parent;
            return child;
        }
        while (cursor[i] == '/') i++;
        parent = child;
        cursor += i;
        dpr("Checking child");
    }
}

void fs2_init_inode(fs2_volume_t* vol, fs2_inode_t* inode, fs2_ftype_t ftype, fs2_fperm_t fperm) {
    dpr("Initialising new inode.");
    inode->ftype = ftype;
    inode->fperm = fperm;
    inode->eof   = 0;
    memset(inode->reg_start, 0, 92);
    if (ftype == FS2_FTYPE_DIR) {
        //Init as directory, including creating a region for entries 
        uint32_t rst = fs2_find_free_region(vol, vol->hblock.default_dreg_len, false);
        if (rst == 0) {
            vol->outcome = FS2_DISK_FULL;
            return;
        }
        inode->reg_start[0] = rst;
        inode->reg_len  [0] = vol->hblock.default_dreg_len;
    }
    else {

    }
    vol->outcome = FS2_SUCCESS;
}

// Adds the entry, but doesn't save the iblock. Use in functions that are going
// to save the iblock anyway.
void fs2_add_dir_entry(fs2_volume_t* vol, char* name, fs2_inode_t* dinode, uint32_t ciindex) {
    dpr("Adding entry to dblock.");
    uint32_t n,i,b = 0;
        while (i < 23 && dinode->reg_start[i] != 0) {
            b = n + dinode->reg_len[i];
            if (dinode->eof < (b * 128)) {
                uint32_t dbaddr = dinode->reg_start[i]
                                + (dinode->eof / 128)
                                - n;
                fs2_load_dblock_c(vol, dbaddr, false);
                if (vol->outcome != FS2_SUCCESS) return;
                // if (!fs2_rblk(vol, dbaddr, (uint8_t*) &dblock)) {
                //     vol->outcome = FS2_DISK_RD_ERR;
                //     return;
                // }
                fs2_dir_entry_t* de = &vol->dcache.entries[dinode->eof % 128];
                strncpy(de->name, name, 28);
                de->inode_index = ciindex;
                fs2_save_dblock_c(vol);
                if (vol->outcome != FS2_SUCCESS) return;
                dinode->eof++;
                // outcome already == SUCCESS
                return;
            }
            n = b;
            i++;
        }
    // No region found for entry
    if (i == 23) {vol->outcome=FS2_FILE_FULL; return;}
    uint32_t daddr = fs2_find_free_region(vol, vol->hblock.default_dreg_len, false);
    if (daddr == 0) return; // Outcome will remain in vol
    dpr(" Had to allocate new dblock.");
    dinode->reg_start[i] = daddr;
    dinode->reg_len  [i] = vol->hblock.default_dreg_len;
    dinode->eof++;
    fs2_load_dblock_c(vol, daddr, true);
    // We can assume FS2_SUCCESS as dry was specified
    
    strncpy(vol->dcache.entries[0].name, name, 28);
    vol->dcache.entries[0].inode_index = ciindex;
    fs2_save_dblock_c(vol);
}

// Doesn't perform any duplicacy checks, and assumes that the parent index
// given is both valid and a directory
void fs2_create_file(fs2_volume_t* vol, char* name, uint32_t parent, fs2_ftype_t ftype) {
    dpr(ftype ? "Creating new file" : "Creating new dir");
    // fs2_iblock_t iblock;
    uint32_t iindex  = vol->hblock.next_inode;
    uint32_t ibindex = iindex / 32;
    fs2_get_iblock_c(vol, ibindex, true, false);
    // uint32_t ibaddr  = fs2_get_iblock(vol, &iblock, ibindex, false);
    if (vol->outcome != FS2_SUCCESS) return;
    // Load was a success
    fs2_inode_t* inode = &vol->icache.inodes[iindex % 32];
    fs2_init_inode(vol, inode, ftype, vol->hblock.default_perm);
    inode->iparent = parent;
    if (vol->outcome != FS2_SUCCESS) return;
    if (ftype == FS2_FTYPE_DIR) {
        dpr("Populating dir with . and .. entries.");
        uint32_t dbaddr = inode->reg_start[0];
        fs2_load_dblock_c(vol, dbaddr, true);
        vol->dcache.entries[0].inode_index = iindex;
        vol->dcache.entries[1].inode_index = parent;
        strcpy(vol->dcache.entries[0].name, "." );
        strcpy(vol->dcache.entries[1].name, "..");
        inode->eof = 2;

        fs2_save_dblock_c(vol);
    }
    if (iindex != 0) { //If not root, add link from parent
        dpr("Linking parent to new file.");
        // Save changes to icache if different to that of dir
        fs2_get_iblock_c(vol, parent / 32, false, true);
        if (vol->outcome != FS2_SUCCESS) return;
        fs2_add_dir_entry(vol, name, &vol->icache.inodes[parent % 32], iindex);
    }
    fs2_save_iblock_c(vol);
    // Save iblock and hblock. dblock already saved if dir.
    vol->hblock.next_inode++;
    if  (fs2_wblk(vol, 0, (uint8_t*) &vol->hblock)) {
        vol->outcome = FS2_SUCCESS;
        dpr("File has been created.");
        return;
    }
    vol->outcome = FS2_DISK_WR_ERR;
}

//////////////////////////
// TOP LEVEL FUNCTIONS //
////////////////////////

// Create a new volume at the given block address and save it.
void fs2_format_volume(fs2_volume_t* vol, uint32_t nblocks, 
        fs2_fperm_t default_perms, uint8_t def_i_sz, uint8_t def_f_sz, 
        uint8_t def_d_sz) {
    
    // The minimum functional size is (hblock + 1 iregion + 1 dregion) for a fs
    // with only a root directory
    if ((nblocks < ( 1 + def_i_sz + def_d_sz))
        || def_d_sz == 0
        || def_f_sz == 0
        || def_i_sz == 0) {
        vol->outcome = FS2_BAD_FORMAT_ARGS;
        return;
    }

    strncpy(vol->hblock.ident, "CWFS 2.1", 8);
    vol->hblock.nblocks          = nblocks;
    vol->hblock.next_block       = 1;
    vol->hblock.next_inode       = 0;
    vol->hblock.default_dreg_len = def_d_sz;
    vol->hblock.default_freg_len = def_f_sz;
    vol->hblock.default_ireg_len = def_i_sz;
    vol->hblock.default_perm     = default_perms;
    vol->hblock.separator        = 800;

    // A region with a start value of 0 indicates it is unused.
    memset(vol->hblock.reg_start, 0, 3200);
    
    fs2_create_file(vol, "", 0, FS2_FTYPE_DIR);
}

void fs2_block_dump(fs2_volume_t* vol, char* x, int nchars) {
    memset(x, '~', nchars);
    bool full = (nchars >= vol->hblock.nblocks + 2);
    if (nchars > 0) x[0] = '[';
    x++; nchars--;
    if (nchars > 0) x[0] = 'H';
    fs2_iblock_t iblock;
    int i,j,k,bi;
    for (i = 0; i < vol->hblock.next_inode; i++) {
        if ((i % 32) == 0) {
            bi = fs2_get_iblock(vol, &iblock, (i / 32), false);
            if (nchars > bi) x[bi] = 'i';
        }
        fs2_inode_t* inode = &iblock.inodes[i % 32];
        for (j = 0; j < 23 && inode->reg_start[j] != 0; j++) {
            for (k = 0; k < inode->reg_len[j]; k++)
                if (nchars > (inode->reg_start[j]+k))
                    x[inode->reg_start[j]+k] = (inode->ftype == FS2_FTYPE_DIR)
                                             ? 'D' : 'F';
        }
    }
    for (i = 0; i < vol->hblock.separator && vol->hblock.reg_start[i] != 0; i++)
        for (j = 0; j < vol->hblock.reg_len[i]; j++)
            if (nchars > vol->hblock.reg_start[i] + j)
                x[vol->hblock.reg_start[i]+j] = 'i';
    if (full) x[nchars-1] = ']';
}

uint32_t find_blk(uint32_t* reg_start, uint8_t* reg_len, int nreg, int bindex) {
    int n = 0; int j = 0; int i = 0;
    while (i < nreg && reg_start[i] != 0) {
        j = n + reg_len[i];
        if (bindex < j) return reg_start[i] + bindex - n;
        n = j;
        i++;
    }
    return 0;    
}

// Unlinks the file and saves the parent's corresponding dblock
void fs2_unlink(fs2_volume_t* vol, fs2_inode_t* pinode, uint32_t xiindex) {
    int i,j,k; i = 0; j = 0; k = 0;
    // fs2_dblock_t dblock, db2;
    uint32_t dbaddr;
    while (k < pinode->eof) {
        if ((k % 128) == 0) {
            // Load next dblock
            dbaddr = pinode->reg_start[i] + j;
            // if (!fs2_rblk(vol, dbaddr, (uint8_t*) &dblock)) 
            fs2_load_dblock_c(vol, dbaddr, false);
            if (vol->outcome != FS2_SUCCESS) return;
                // {vol->outcome = FS2_DISK_RD_ERR; return;}
            i += ((j = (j+1) % pinode->reg_len[i]) == 0);
        }
        if (vol->dcache.entries[k % 32].inode_index == xiindex) {
            // Remove this and replace it with the last entry in the dir.
            // If it is said entry, just decrement eof
            uint32_t last = pinode->eof - 1;
            if (k != last) {
                fs2_dir_entry_t* le;
                if ((k / 128) != (last / 128)) {
                    int b = 0;
                    uint32_t ldbaddr = 
                        find_blk(pinode->reg_start, pinode->reg_len, 23, (last / 128));
                    fs2_load_gp_c(vol, ldbaddr, false);
                    if (vol->outcome != FS2_SUCCESS) return;
                    fs2_dblock_t* dblock = (fs2_dblock_t*) &vol->xcache;
                    le = &dblock->entries[last % 128];
                } else le = &vol->dcache.entries[last % 128];
                memcpy (&vol->dcache.entries[k%128], le, 32);
            }
            pinode->eof--;
            fs2_save_dblock_c(vol);
            return;
        }
        k++;
    }
    // Here be dragons  
    // If this point is reached, the FS is corrupt as the parent has no link to
    // the file.
    vol->outcome = FS2_NO_FILE;
}

bool fs2_rm(fs2_volume_t* vol, char* path) {
    uint32_t p;
    uint32_t ind = fs2_find_file(vol, path, &p);
    if (vol->outcome != FS2_SUCCESS) return false;
    // File was found.
    if (ind == 0) {
        // Can't delete root!
        vol->outcome = FS2_BAD_PERMISSIONS;
        return false;
    }
    fs2_get_iblock_c(vol, ind / 32, false, false);
    if (vol->outcome != FS2_SUCCESS) return false;
    // iblock is loaded
    fs2_inode_t* inode = &vol->icache.inodes[ind % 32];
    if (inode->ftype == FS2_FTYPE_DIR && inode->eof > 2)
        // Non-empty dir
        {vol->outcome = FS2_BAD_FTYPE; return false;}
    bool move = ind < vol->hblock.next_inode - 1;
    if (move) {
        memset(inode->reg_start, 0, 92);
      ////////////////////////////////////////////////////////////////////////
      // Keep inodes contiguous by moving the last one to the location of  //
      // the file to be deleted                                           //
      // Non-essential feature: not implementing this yet. For now we    //
      // can deal with gaps in iregions.                                //
      ///////////////////////////////////////////////////////////////////
    }
    // inode is either a file OR an empty dir. Unlink it from its parent
    fs2_get_iblock_c(vol, p / 32, false, move);
    if (vol->outcome != FS2_SUCCESS) return false;
    // Now inode will point to the parent
    inode = &vol->icache.inodes[p % 32];
    fs2_unlink(vol, inode, ind);
    if (vol->outcome != FS2_SUCCESS) return false;
    fs2_save_iblock_c(vol);
    return vol->outcome == FS2_SUCCESS;    
}

bool fs2_ls(fs2_volume_t* vol, char* path, char* out, int nchars) {
    uint32_t iindex = fs2_find_file(vol, path, NULL);
    if (vol->outcome != FS2_SUCCESS) return false;

    fs2_get_iblock_c(vol, iindex / 32, false, false);

    if (vol->outcome != FS2_SUCCESS) return false;

    fs2_inode_t* dinode;
    dinode = &vol->icache.inodes[iindex % 32];

    int k = 0; int i = 0; int j = 0;
    char* cursor = out;
    fs2_dir_entry_t* de;
    while (k < dinode->eof) {
        if ((k % 128) == 0) {
            // Load next dblock
            fs2_load_dblock_c(vol, dinode->reg_start[i] + j, false);
            if (vol->outcome != FS2_SUCCESS) return false;
            i += ((j = ((j + 1) % dinode->reg_len[i])) == 0);
        }
        de = &vol->dcache.entries[k % 128];
        for (int s = 0; (s < 28) && (de->name[s] != '\0'); s++) {
            *cursor++ = de->name[s];
            if ((cursor - out) >= nchars) return false;
        }
        if ((cursor - out) < nchars) *cursor++ = '\n';
        else return false;
        k++;
    }
    if ((cursor - out) < nchars) *cursor = '\0';
    else return false;
    vol->outcome = FS2_SUCCESS;
    return true;
}

bool fs2_chmod(fs2_volume_t* vol, char* path, fs2_fperm_t newp) {

}

bool fs2_isftype(fs2_volume_t* vol, char* path, fs2_ftype_t ftype) {
    uint32_t iindex = fs2_find_file(vol, path, NULL);
    if (vol->outcome != FS2_SUCCESS) return false;
    fs2_get_iblock_c(vol, iindex / 32, false, false);
    if (vol->outcome != FS2_SUCCESS) return false;

    return vol->icache.inodes[iindex % 32].ftype == ftype;
}

int fs2_read(fs2_volume_t* vol, char* path, uint8_t* out, uint32_t nbytes, uint32_t cursor) {
    uint32_t iindex = fs2_find_file(vol, path, NULL);
    if (vol->outcome != FS2_SUCCESS) return -1;

    fs2_get_iblock_c(vol, iindex / 32, false, false);
    if (vol->outcome != FS2_SUCCESS) return -1;
    fs2_inode_t* inode = &vol->icache.inodes[iindex % 32];

    if (inode->ftype != FS2_FTYPE_FILE) {
        vol->outcome = FS2_BAD_FTYPE;
        return -1;
    }
    if (inode->fperm & FS2_U_READ == 0) {
        vol->outcome = FS2_BAD_PERMISSIONS;
        return -1;
    }
    // Number of bytes that can be read
    int maxbytes = inode->eof - cursor;
    if (maxbytes <= 0) return 0;

    int blkind = cursor / FS2_BLOCK_SZ;
    uint32_t blk = find_blk(inode->reg_start, inode->reg_len, 23, blkind);
    if (blk == 0) {
        // The cursor points outside the file; not technically an error so just
        // return 0 as no bytes can be read.
        return 0;
    }
    // Actual number of bytes to be read
    int rbytes = nbytes < maxbytes ? nbytes : maxbytes;
    int rcount = 0;
    // Number of bytes that can be read in the first block
    int maxb1  = FS2_BLOCK_SZ - (cursor % FS2_BLOCK_SZ);
    int read1  = rbytes > maxb1 ? maxb1 : rbytes;

    fs2_load_gp_c(vol, blk, false);
    if (vol->outcome != FS2_SUCCESS) return -1;
    memcpy(out, &vol->xcache.bytes[cursor % FS2_BLOCK_SZ], read1);
    rcount += read1;

    if (rcount == rbytes) return rcount;

    while (rbytes - rcount >= FS2_BLOCK_SZ) {
        blk = find_blk(inode->reg_start, inode->reg_len, 23, ++blkind);
        if (blk == 0) return rcount;
        fs2_load_gp_c(vol, blk, false);
        if (vol->outcome != FS2_SUCCESS) return rcount;
        memcpy(out + rcount, &vol->xcache, FS2_BLOCK_SZ);
        rcount += FS2_BLOCK_SZ;
    }

    if (rcount == rbytes) return rcount;
    blk = find_blk(inode->reg_start, inode->reg_len, 23, ++blkind);
    fs2_load_gp_c(vol, blk, false);
    if (vol->outcome != FS2_SUCCESS) return rcount;
    memcpy(out + rcount, &vol->xcache, rbytes - rcount);
    return rbytes;
}

uint32_t fs2_grow_file(fs2_volume_t* vol, fs2_inode_t* inode, uint8_t rsz) {
    int i = 0;
    while (i < 23 && inode->reg_start[i] != 0) ++i;
    if (i == 23) {
        vol->outcome = FS2_FILE_FULL;
        return 0;
    }
    uint32_t blk0 = fs2_find_free_region(vol, rsz, true);
    if (blk0 == 0 || vol->outcome != FS2_SUCCESS) return 0;
    inode->reg_start[i] = blk0;
    inode-> reg_len [i] = rsz;
    vol->outcome = FS2_SUCCESS;
    return blk0;
}

int finish_write(fs2_volume_t* vol, fs2_inode_t* inode, int cursor, int wcount) {
    inode->eof = cursor + wcount;
    fs2_save_iblock_c(vol);
    return wcount;
}

// Assumes cursor is valid ie <= eof. Always updates EOF
int fs2_write(fs2_volume_t* vol, char* path, uint8_t* in, uint32_t nbytes, uint32_t cursor) {
    // Top section identical to read(…) - extract to separate function?
    uint32_t iindex = fs2_find_file(vol, path, NULL);
    if (vol->outcome != FS2_SUCCESS) return -1;

    fs2_get_iblock_c(vol, iindex / 32, false, false);
    if (vol->outcome != FS2_SUCCESS) return -1;
    fs2_inode_t* inode = &vol->icache.inodes[iindex % 32];

    if (inode->ftype != FS2_FTYPE_FILE) {
        vol->outcome = FS2_BAD_FTYPE;
        return -1;
    }
    if (inode->fperm & FS2_U_WRITE == 0) {
        vol->outcome = FS2_BAD_PERMISSIONS;
        return -1;
    }

    int wcount = 0;

    int max1   = FS2_BLOCK_SZ - (cursor % FS2_BLOCK_SZ);
    int write1 = nbytes > max1 ? max1 : nbytes;

    int blkind = cursor / FS2_BLOCK_SZ;
    // 1. Write first (partial) block
    uint32_t blk = find_blk(inode->reg_start, inode->reg_len, 23, blkind);
    if (blk == 0) {
        uint8_t sz = nbytes / FS2_BLOCK_SZ;
        if (nbytes % FS2_BLOCK_SZ) ++sz;
        blk = fs2_grow_file(vol, inode, sz);
        if (vol->outcome != FS2_SUCCESS) return 0;
    }
    fs2_load_gp_c(vol, blk, false);
    if (vol->outcome != FS2_SUCCESS) return 0;
    memcpy(&vol->xcache.bytes[cursor % FS2_BLOCK_SZ], in, write1);
    fs2_save_gp_c(vol);
    if (vol->outcome != FS2_SUCCESS) return 0; //Abort write: changes may or may not persist
    wcount += write1;

    // 2. Write full blocks (skipped if no full blocks)
    while ((nbytes - wcount) >= 4096) {
        blk = find_blk(inode->reg_start, inode->reg_len, 23, ++blkind);
        if (blk == 0) {
            int rembytes = nbytes - wcount;
            uint8_t sz = rembytes / FS2_BLOCK_SZ;
            // include extra block if not an exact block length
            if (rembytes % FS2_BLOCK_SZ) ++sz;
            blk = fs2_grow_file(vol, inode, sz);
            if (vol->outcome != FS2_SUCCESS) return finish_write(vol, inode, cursor, wcount);
        }
        // Overwriting full block anyway so don't need to actually read it
        fs2_load_gp_c(vol, blk, true);
        // Can assume success on dry load
        memcpy (&vol->xcache, in + wcount, FS2_BLOCK_SZ);
        fs2_save_gp_c(vol);
        if (vol->outcome != FS2_SUCCESS) return finish_write(vol, inode, cursor, wcount);
        wcount += FS2_BLOCK_SZ;
    }
    if (wcount == nbytes) return finish_write(vol, inode, cursor, wcount);

    // 3. Write last (partial) block
    blk = find_blk(inode->reg_start, inode->reg_len, 23, ++blkind);
    if (blk == 0) {
        // Only one block is required, but use default reg length
        blk = fs2_grow_file(vol, inode, vol->hblock.default_freg_len);
        if (vol->outcome != FS2_SUCCESS) return finish_write(vol, inode, cursor, wcount);
    }
    // Anything after the end of the write will be disregarded, so no need to
    // load the original block
    fs2_load_gp_c(vol, blk, true);
    memcpy(&vol->xcache, in, nbytes - wcount);
    fs2_save_gp_c(vol);
    return finish_write(vol, inode, cursor, vol->outcome == FS2_SUCCESS ? nbytes : wcount);
}

// Create a file, or directory at the given path.
// Explanation of potentially ambiguous outcomes:
//  FS2_NO_FILE   : The parent directory could not be found.
//  FS2_BAD_FTYPE : A file was found where the parent dir was expected.
// Returns true if the file already exists, or if it was successfully created.
// False otherwise.
bool fs2_create(fs2_volume_t* vol, fs2_ftype_t ftype, const char* pathc) {
    // To support modifying the path, create a copy
    char path[strlen(pathc)+1];
    strcpy(path, pathc);
    uint32_t p = fs2_find_file(vol, path, NULL);
    if (vol->outcome == FS2_SUCCESS) {
        // File exists!
        vol->outcome = FS2_UNEXPECTED_FILE;
        return false;
    }
    // The file was not found, but not because it doesn't exist
    if (vol->outcome != FS2_NO_FILE) return false; 
    // Now check if the parent exists
    char* split = path + strlen(path) - 1;
    if (*split == '/') {
        vol->outcome = FS2_INVALID_PATH; return false;
    }
    while (split > path && *split != '/') split--;
    char* new = split == path ? split : split + 1;
    while (split > path && *split == '/') {
        *split = '\0'; split--;
    }
    // The below check is needed as otherwise path and split could be the same
    // string, as a terminator has not been inserted. Root is always iindex 0.
    if (split == path) p = 0;
    // At this point, the path variable points to that of the parent, and new
    // is the name of the new file
    else {
        p = fs2_find_file(vol, path, NULL); // Does the parent exist?
        if (vol->outcome != FS2_SUCCESS) return false;
    }
    fs2_create_file(vol, new, p, ftype);
    if (vol->outcome != FS2_SUCCESS) return false;
    return true;
}

char* fs2_outcome_str(fs2_volume_t* vol) {
    switch (vol->outcome) {
        case FS2_SUCCESS:
            return "Operation successful";
        case FS2_BAD_FTYPE:
            return "A file or dir was found where the other was expected";
        case FS2_DISK_FULL:
            return "There is no more free space on this volume";
        case FS2_DISK_RD_ERR:
        case FS2_DISK_WR_ERR:
            return "Input/output error";
        case FS2_INVALID_PATH:
            return "The path provided is of a bad format";
        case FS2_NO_FILE:
            return "A file was not found where one was expected";
        case FS2_UNEXPECTED_FILE:
            return "A file was found where one was not expected";
        case FS2_FILE_FULL:
            return "The file/dir has used all of its available regions";
        default:
            return "?";  
    }
}
