/* Copyright (C) 2019 Jonah McPartlin
 *
 * Use of this source code is restricted per the CC BY-NC-ND license, a copy of 
 * which can be found via http://creativecommons.org (and should be included as 
 * LICENSE.txt within the associated archive or repository).
 */

#include "libc.h"
#include "xlibc.h"
#include "strformat.h"

#define PHIL_COUNT 16
// In this method, the 'last' philosopher will reach for their right fork
// before their left, while all others are the opposite.
// This method supports truly parallel execution by avoiding a situation in 
// which all philosophers have picked up their left fork and are waiting for
// their right.
#define  FIRST_FORK (left_first ? left_fork : right_fork)
#define SECOND_FORK (left_first ? right_fork : left_fork)

extern int is_prime(uint32_t x);
void eat() {
    //Use routine from P5 to take some time
    uint32_t lo = 1 <<  8;
    uint32_t hi = 1 << 16;

    for( uint32_t x = lo; x < hi; x++ ) {
        int r = is_prime( x ); 
    }
}

// Outputs generated from the philosophers will 
void phil_thinker(sem_id_t left_fork) {
    sem_id_t right_fork = (left_fork == PHIL_COUNT - 1) ? 0 : left_fork + 1;

    int left_first = left_fork < right_fork;
    while(1) {
        //Think until first fork available
        printn(" ☹ ", 5);
        sem_wait(FIRST_FORK, 1);
        printn(left_first ? "\\☹ " : " ☹/", 5);
        
        //Think until second fork available
        sem_wait(SECOND_FORK, 1);
        printn("\\☺/", 5);

        //Eat!
        eat();
        
        //Put forks down (order doesn't matter)
        sem_post(FIRST_FORK, 1);
        sem_post(SECOND_FORK, 1);
    }
}

void phil_spawner() {
    int i;
    //Init semaphores as mutexes
    for (i = 0; i < PHIL_COUNT; i++) sem_init(i, 1);
    int ps[PHIL_COUNT];
    int pfds[2];
    i = 0;
    while (i < PHIL_COUNT) {
        pipe(pfds); //Create a pipe for each philosopher
        //The child process starts thinking and eating
        if (!fork()) {
            close(pfds[0]); //Close read end of pipe
            fd_swap(STDOUT_FILENO, pfds[1]); //redirect stdout to pipe
            phil_thinker(i);
        }
        close(pfds[1]); //Close write end of pipe
        ps[i] = pfds[0]; //Keep read end fd
        i++;
    }
    // I am the original spawner process. The philosophers send me their
    // pipes.
    int n = 5 * PHIL_COUNT + 1;
    char table[n];
    int tr; //total read
    memset(table, ' ', n-1);
    table[n-1] = '\n';
    while (1) {
        tr = 0;
        for (i = 0; i < PHIL_COUNT; ++i) {
            tr += read(ps[i], table + (5 * i), 5);
        }
        if (tr > 0) write(STDOUT_FILENO, table, n);
        // There is no point returning to the beginning of the loop yet, so
        yield();
    }
    exit(EXIT_SUCCESS);
}
