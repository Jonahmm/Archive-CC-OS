/* Copyright (C) 2019 Jonah McPartlin
 *
 * Use of this source code is restricted per the CC BY-NC-ND license, a copy of 
 * which can be found via http://creativecommons.org (and should be included as 
 * LICENSE.txt within the associated archive or repository).
 */

#define PIPE_SIZE 0x1000

#include <stdint.h>
#include <stdbool.h>

//Pipe
typedef struct {
  char buffer[PIPE_SIZE];
  uint32_t reader;
  uint32_t writer;
  bool full;
  int reader_pid;
  int writer_pid;
} pipe_t;

void pipe_reset (pipe_t* pipe);

int  pipe_read (pipe_t* pipe, char* out, int nchars);
int  pipe_write (pipe_t* pipe, char* in, int nchars);