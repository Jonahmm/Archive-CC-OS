/* Copyright (C) 2019 Jonah McPartlin
 *
 * Use of this source code is restricted per the CC BY-NC-ND license, a copy of 
 * which can be found via http://creativecommons.org (and should be included as 
 * LICENSE.txt within the associated archive or repository).
 */
#include <stdint.h>
#include <stdbool.h>

#define FS2_BLOCK_SZ 0x1000

#define FS2_FTYPE_DIR  0x00
#define FS2_FTYPE_FILE 0x01

typedef uint8_t fs2_ftype_t;
typedef uint8_t fs2_fperm_t;

#define FS2_U_READ  0x01
#define FS2_U_WRITE 0x02
#define FS2_U_EXEC  0x04
// Using same region specifiers from first FS: 32 bits giving first block, and
// 8 bits giving size

// Regions are one of:
// • iregions - holding inodes
// • dregions - holding directory entries and their inode indexes
// • fregions - holding file data

// Example volume structure (each char is a block)
// 0   4   8   12  16 … 256
// ↓   ↓   ↓   ↓   ↓    ↓
//[HIIIID11D2D233*4***…*]
// 
// Block 0 is the header block (hblock). It holds volume metadata including
// the size of the volume (in the example, 256 blocks), the start of the unused
// space in the volume (16), the default [i,f,d]region sizes, default
// permissions, and the locations and sizes of iregions
// 
// Blocks 1-4 are an iregion of size 4. It can hold up to 128 inodes, at which
// point a new iregion will have to be allocated by the system. 
// 
// Block 5 shows a dregion of size 1, holding 128 entries. If
// the volume's dregion size were set higher (in the example it is 1) the
// region would take up more than this single block. This represents the dir in
// inode #0
// 
// Blocks 6,7 are an fregion corresponding to inode #1.
// The file at inode #2 has been split across two 1-block regions, 9 and 11.

typedef struct {
    // Type: dir or file
    fs2_ftype_t ftype;      //1
    // Can user mode processes rwx?
    fs2_fperm_t fperm;      //2
    uint8_t  unused[3];     //5
    uint8_t  reg_len  [23]; //28
    // eof byte index for files, #entries for dirs
    uint32_t eof;           //32
    // Didn't want to include this, but keeps unlinking simple.
    uint32_t iparent;       //36
    // Regions holding the file data / dir entries
    uint32_t reg_start[23]; //128
} fs2_inode_t;

typedef struct {
    uint32_t inode_index; //4
    char name[28];        //32
} fs2_dir_entry_t;

typedef struct { uint8_t bytes[4096];}           fs2_block_t;
typedef struct { fs2_inode_t inodes[32];}       fs2_iblock_t;
typedef struct { fs2_dir_entry_t entries[128];} fs2_dblock_t;

typedef struct {   
    // Volume identifier
    char     ident[8];              //8
    // Default permissions
    fs2_fperm_t  default_perm;      //9
    // Default iregion length
    uint8_t  default_ireg_len;      //10
    // Default fregion length
    uint8_t  default_freg_len;      //11
    // Default dregion length
    uint8_t  default_dreg_len;      //12
    // Maximum size of the volume
    uint32_t nblocks;               //16
    // Index of the next free inode.
    // == no. of files on volume
    uint32_t next_inode;            //20
    // Block index of next free block.
    // == size of volume 
    uint32_t next_block;            //24
    // Unused area
    int8_t   unused [70];           //94

    // Marks the point in the region table at which all succeeding entries
    // represent free regions
    uint16_t separator;             //96
    // Starting block and length of regions:
    // iregions are stored from the bottom, free regions from the top.
    uint32_t reg_start [800];       //3296
    uint8_t  reg_len   [800];       //4096
} fs2_hblock_t;

typedef enum {
    FS2_SUCCESS,
    // Not a CWFS2 volume
    FS2_INVALID_FS,
    // Invalid format parameters
    FS2_BAD_FORMAT_ARGS,
    // Hardware/communication errors
    FS2_DISK_RD_ERR,
    FS2_DISK_WR_ERR,
    // Space errors
    FS2_HBLOCK_FULL, // There is no space in the hblock to register new regions
    FS2_DISK_FULL,   // There is no free region of the size given on the disk
    FS2_FILE_FULL,   // The file/dir has used all of its available regions

    FS2_UNEXPECTED_FILE,
    FS2_INVALID_PATH,       // The path given is of a bad format
    FS2_NO_FILE,            // There is no file at the given path
    FS2_BAD_FTYPE,          // The file type is not as expected
    FS2_BAD_PERMISSIONS     // Cannot r/w/(x) this file in user mode
} fs2_outcome_t;

typedef struct {
    uint32_t        blk_0;  // First block in the volume
    fs2_outcome_t outcome;  // Outcome of most recent op
    fs2_hblock_t   hblock;  // Copy of the volume's hblock in memory
    // Caches: Unless modified in a method, these are always assumed to be
    // both up to date and saved
    // iblock
    uint32_t      icindex;
    uint32_t       icaddr;
    fs2_iblock_t   icache;
    // dblock
    uint32_t       dcaddr;
    fs2_dblock_t   dcache;
    // general purpose block: Not assumed to be valid
    uint32_t       xcaddr;
    fs2_block_t    xcache;
} fs2_volume_t;


void fs2_format_volume(fs2_volume_t* vol, uint32_t nblocks, 
        fs2_fperm_t default_perms, uint8_t def_i_sz, uint8_t def_f_sz, 
        uint8_t def_d_sz);

void fs2_load_volume(fs2_volume_t* vol);

void fs2_block_dump (fs2_volume_t* vol, char* x, int nchars);

void fs2_create_directory(fs2_volume_t* vol, char* name, uint32_t parent);

uint32_t fs2_find_file(fs2_volume_t* vol, char* path, uint32_t* putparent);

bool fs2_create(fs2_volume_t* vol, fs2_ftype_t ftype, const char* path);

char* fs2_outcome_str(fs2_volume_t* vol);

bool fs2_rm(fs2_volume_t* vol, char* path);

void fs2_dbg_tree(fs2_volume_t* vol, char* x, int nchars, int max);


bool fs2_isftype(fs2_volume_t* vol, char* path, fs2_ftype_t ftype);

// VFS INTERFACE
bool fs2_ls    (fs2_volume_t* vol, char* path, char* out, int nchars);
int  fs2_read  (fs2_volume_t* vol, char* path, uint8_t* out, uint32_t nbytes, uint32_t cursor);
int  fs2_write (fs2_volume_t* vol, char* path, uint8_t*  in, uint32_t nbytes, uint32_t cursor);
