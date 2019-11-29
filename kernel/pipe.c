/* Copyright (C) 2019 Jonah McPartlin
 *
 * Use of this source code is restricted per the CC BY-NC-ND license, a copy of 
 * which can be found via http://creativecommons.org (and should be included as 
 * LICENSE.txt within the associated archive or repository).
 */

#include "pipe.h"

void pipe_reset(pipe_t* pipe) {
    pipe->reader     = 0;
    pipe->writer     = 0;
    pipe->full       = false;
    pipe->reader_pid = -1;
    pipe->writer_pid = -1;
}

bool can_read(pipe_t* pipe) {
    return  pipe->full || (pipe->writer != pipe->reader);
}

int pipe_read(pipe_t* pipe, char* out, int nchars) {
    int i;
    while (i < nchars && can_read(pipe)) {
        out[i] = pipe->buffer[pipe->reader];
        i++;
        pipe->reader = (pipe->reader + 1) % PIPE_SIZE;
        pipe->full = false;
    }
    return i;
}

int pipe_write(pipe_t* pipe, char* in, int nchars) {
    int i;
    while (i < nchars && !pipe->full) {
        pipe->buffer[pipe->writer] = in[i];
        i++;
        pipe->writer = (pipe->writer + 1) % PIPE_SIZE;
        pipe->full = pipe->writer == pipe->reader;
    }
    return i;
}