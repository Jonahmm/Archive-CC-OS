/* Copyright (C) 2019 Jonah McPartlin
 *
 * Use of this source code is restricted per the CC BY-NC-ND license, a copy of 
 * which can be found via http://creativecommons.org (and should be included as 
 * LICENSE.txt within the associated archive or repository).
 */

// eXtended LIBrary for C system calls

#include <stdbool.h>
#include <stdint.h>

#define SEM_OPEN 0x08
#define SEM_POST 0x09
#define SEM_WAIT 0x0A

#define OPEN     0x0B
#define CLOSE    0x0C
#define FD_SWAP  0x0D

#define PIPE     0x0E
// Exec, with args
#define EXECX    0x0F

#define ISFILE   0x10
#define ISDIR    0x11
#define CD       0x12
#define LS       0x13
#define RM       0x14
#define MKFILE   0x15
#define MKDIR    0x16
#define CHMOD    0x17
#define GETWD    0x18

#define F_READ   0x1
#define F_WRITE  0x2
#define F_CREATE 0x4

typedef uint32_t sem_id_t;

//Set the semaphore given by sem_id to the initial value init
bool sem_init(sem_id_t sem_id, uint32_t init);

//Increment semaphore sem_id by x
void sem_post(sem_id_t sem_id, uint32_t x);

//Wait for semaphore sem_id to by ≥x, then decrement it by x and return
void sem_wait(sem_id_t, uint32_t x);

//Attempt to open the file at the given path, returning a file descriptor or -1
int  open   (char* path, char flags);
//Close the file given by fd
bool close  (int fd);

bool fd_swap(int fd1, int fd2);

bool pipe   (int*  pfds);

void execx  (void* addr, uint32_t arg);

bool cd     (char* path);
bool ls     (char* path, char* out, int nchars);
bool rm     (char* path);
bool isfile (char* path);
bool isdir  (char* path);
bool mkfile (char* path);
bool mkdir  (char* path);
bool getwd  (char* out, int nchars);
