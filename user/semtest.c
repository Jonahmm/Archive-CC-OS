/* Copyright (C) 2019 Jonah McPartlin
 *
 * Use of this source code is restricted per the CC BY-NC-ND license, a copy of 
 * which can be found via http://creativecommons.org (and should be included as 
 * LICENSE.txt within the associated archive or repository).
 */

#include "libc.h"
#include "xlibc.h"

extern int is_prime(uint32_t x);
void waste_time() {
    uint32_t lo = 1 <<  8;
    uint32_t hi = 1 << 16;

    for( uint32_t x = lo; x < hi; x++ ) {
      int r = is_prime( x ); 
    }
}

void main_semtest() {
    sem_init(0, 1); //Initialise semaphore 0 as a mutex

    sem_wait(0, 1); //Grab mutex

    if (fork()) {
        //I am parent, I already have the mutex
        while(1) {
            waste_time();
            write(STDOUT_FILENO, " (p)think ", 10);
            sem_post(0, 1);
            //If another process is waiting for the mutex, it will now be
            //marked as ready and the value updated to match. If this is the
            //case, the below statement will block
            // write(STDOUT_FILENO, "(p)wait", 7);
            sem_wait(0, 1);
        }
    } else {
        //I am child, I must wait for mutex
        while(1) {
            // write(STDOUT_FILENO, "(c)wait\n", 8);
            //Wait for mutex
            sem_wait(0, 1);
            waste_time();
            write(STDOUT_FILENO, " (c)think ", 10);
            sem_post(0 ,1);
        }
    }
}