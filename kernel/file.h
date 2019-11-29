#include <stdint.h>

typedef uint32_t fd_t;

/////
////  FILES AND DESCRIPTORS
///
//File type
typedef enum {
  FT_UART, //StdIO
  FT_KERN, //Pseudofiles for getting sys info
  FT_PIPE,
  FT_FILE
} ftype_t;

//File mode
typedef enum {
  FM_NULL,
  FM_R, //Read only
  FM_W, //Write only
  FM_RW //Read-write
} fmode_t;

//File Descriptor Table Entry
typedef struct {
  int open_count;
  ftype_t type;
  fmode_t mode;
  uint32_t id;
  uint32_t cursor;
} fdte_t;
