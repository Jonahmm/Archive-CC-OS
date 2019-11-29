#include <stdint.h>
#include <stdbool.h>
#include <sys/types.h>
#include <string.h>
#include "disk.h"
// #include "file.h"
#include "fs2.h"

// This fs was abandoned and replaced with fs2.[ch]

// Coursework File System (CWFS) 1.0

// In this file system I assume the disk is separated into 'blocks' of 4KiB
// size. A contiguous region of blocks I call a 'region'. A 'file' can be
// stored in multiple regions, up to a set maximum, at which point either the
// resize will fail OR the disk will have to be defragmented. This is to ensure
// that the size of the 'irecord' data structure containing file (incl. dir) 
// info is a reasonable size, as otherwise it would have to keep track of every
// block in use by a file, which would be impractical as a file grows large.

// The file system is designed in such a way that it requires no statically
// allocated area for fs operations such as a FAT, with the exception of a
// single header block. The minimum functional size of the FS is only 3 blocks:
// A header, a 1-block iregion containing a single file irecord, whose file
// occupies the third block.
// Another feature of the file system is that it requires no constantly running
// driver, though could be enhanced with one that identifies good areas to
// store different sized files, or automatic background defragmentation

// The file system has no inherent localisation between directories and their
// children - files can be scattered around the disk. This seems fine for small
// or embedded applications which would likely use media with no moving parts,
// though would be inefficient on HDD systems

// An example volume layout is shown below, where each character represents a 
// block, with _ denoting void.
// Hi1<<23_i2_…
// 
// H (Block 0) denotes the header block (hblock). It keeps track of:
//  • The size of the volume
//  ( • The next free region and its size (in blocks) )
//  • The highest index in use by a irecord
//    > As irecords are quite small, it makes sense to store them
//      contiguously rather than allow their segmentation. This means that we
//      only need keep track of one piece of information about where to place
//      new irecords.
//  • The region(s) being used for storage of the irecords
//    > Filling to the end of the block, allowing a very large maximum file
//      count.
// 
// i denotes a region being used for irecord storage. In the example, the first
// such region (block 1) has been filled and the fs has had to allocate a
// second (block 10) to allow more files to be created.
// Each irecord is a 128 byte structure keeping track of:
//  • The file's name
//  • The irecord index of the parent directory
//  • Permissions (wrx)
//  • The file type (file, dir, symlink…)
//  • Depending on the above, one of:
//    > An array of region pointers identifying the file body location(s)
//      on the volume
//      AND the byte index of the EOF
//    > An array of irecord indexes identifying any children of the directory
//      (this does impose a limit on the number of files in one directory)
//    > The index of the linked irecord
// 
// A number denotes the beginning of a region belonging to a file, with the
// number itself identifying the file and < denoting the continuity of a region
// into the succeeding block. In the example, file 1 occupies a single
// 3-block region, 2 encompasses two separate 1-block regions, and 3 uses only
// a single 1-block region. 
// 
// A file has been deleted, leaving a a void block between file 3's only region
// and the second irecord block. This block will be used when one is required
// (for a new file or a resize)


//LIMITS
// A file can have up to 18 characters in its name
// A directory can have up to 26 children with 128-bit irecords
// 
// Using 8-bit region size and 128-byte irecords (allowing 4 bytes for eof):
//  A file can use up to 20 regions each of up to 256 blocks, so the max size
//  in theory is 20.9… MB. This has the benefit that a file can be much more 
//  fragmented and so the volume is less likely to need defragmentation on
//  creation of a large file

// Linprog
// (blocksize - 12) / 4 = maxiblocks
// maxiblocks * (blocksize / irecsize) = maxfiles
// 

//Blocks of 4KiB
#define BLOCK_SIZE       0x1000

#define UNUSED_VALUE 0xFFFFFFFF

//20 * 5 byte regions = 100 bytes, +4 for eof = full 104-byte payload
#define MAX_FREGIONS 20
#define MAX_NAME_LENGTH  18
#define MAX_IREGIONS 800
#define MAX_CHILDREN 26

#define IRECORD_SIZE 128
#define PAYLOAD_SIZE 104
#define IRECORDS_PER_BLOCK 32

#define FT_DIR  0
#define FT_FILE 1
#define FT_LINK 2

#define PERM_U_READ  0x1
#define PERM_U_WRITE 0x2
#define PERM_U_EXEC  0x4

//Used for sizeof operations and similar
typedef struct {uint8_t bytes[BLOCK_SIZE];} block_t;
typedef char *  fs_path;
typedef uint32_t blk_id;

//All the potential outcomes of a fs operation
typedef enum {
    SUCCESS,
    INVALID_PATH,
    HBLOCK_FULL,
    READ_ERROR,
    WRITE_ERROR,
    DISK_FULL,
    DIR_FULL,
    BAD_PERMISSIONS,
    NO_SUCH_FILE
} fs_msg_t;

typedef struct {
    fs_msg_t msg;
    uint32_t data;
} fs_response;

//////////////////////
// FILE STRUCTURES //
////////////////////
// ////// 5 byte structure identifying the start and length of a region
// typedef struct {
//     // The index of the first block in the region
//     blk_id     start;
//     // The number of blocks in use (max. 255)
//     uint8_t nblocks;
// } fs_region;

// The above struct brought alignment issues when stored in arrays - regions
// will be stored transposed (i.e. an array of blk_ids then an array of bytes
// denoting region size)

// 4KiB structure holding essential FS information
typedef struct  {
    // Number of blocks in the volume - this will be set on load, not loaded
    // from the disk as the size may change
    uint32_t nblocks;
    // Index of the next free block (the end of the volume)
    blk_id next_blk;
    // The first unused irecord index. This is == the number of files (incl.
    // dirs, symlinks etc) on the disk.
    uint32_t next_irecord;
    //Identifier denoting that this is a CWFS volume
    char ident[8];
    // Default size for iregions and fregions (filesize). If an instance of CWFS
    // is being used to store a lot of small files setting the first to high and
    // the second to low will increase efficiency. Likewise the reverse is true
    // for instances with few, large files 
    uint8_t ireg_ds, freg_ds;
    // Default permissions
    uint8_t perms_d;
    // Unused area. It seems large but if I decide to significantly alter the
    // behaviour then I may need to keep track of more information. This is
    // equivalent to only 18 32-bit ints (and a byte).
    uint8_t unused[73];

    // The regions for storing irecords. Stored as such to avoid padding
    blk_id ireg_start[MAX_IREGIONS];
    uint8_t ireg_size[MAX_IREGIONS];
    // fs_region iregs[MAX_IREGIONS];
} hblock;

typedef struct {
    char name[MAX_NAME_LENGTH];
    char permissions;
    char type;
    // The irecord index of the parent dir
    uint32_t parent;
    // The 104-byte payload: either a fs_pl_file, fs_pl_dir or fs_pl_lnk
    uint8_t payload[104];
    // 128 bytes total
} irecord;

typedef struct { irecord records[IRECORDS_PER_BLOCK]; } iblock_t;

typedef struct {
    uint32_t eof; //The byte index of the end of the file
    //Up to 20 regions can be used by one file.
    blk_id freg_start[MAX_FREGIONS];
    uint8_t freg_size[MAX_FREGIONS];
    // fs_region regions[20];
} fs_pl_file;

typedef struct { uint32_t children[26]; } fs_pl_dir;

typedef struct { uint32_t target; uint32_t unused[25]; } fs_pl_link;

typedef uint32_t file_loc;

typedef struct {
    blk_id blk_0;
    fs_msg_t result;
    hblock header;
} fs_meta;

// //Check if the path is of valid format
// fs_response path_valid  (fs_path path, fs_path cd);

// //Check if there is a file/dir at the path
// fs_response file_exists (fs_path path, fs_path cd);
// fs_response is_file     (fs_path path, fs_path cd);
// fs_response is_dir      (fs_path path, fs_path cd);

// //Create file/dir
// fs_response mkfile      (fs_path path, fs_path cd);
// fs_response mkdir       (fs_path path, fs_path cd);

// //File IO
// fs_response f_write     (fs_path path, fs_path cd, uint8_t* data, int nbytes);
// fs_response f_read      (fs_path path, fs_path cd, uint8_t* data, int nbytes);

bool format_volume(fs_meta* meta, uint32_t nblocks,
                    uint8_t ireg_ds, uint8_t freg_ds, uint8_t perms_d);

bool load_volume(fs_meta* meta);