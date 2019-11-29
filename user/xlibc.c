/* Copyright (C) 2019 Jonah McPartlin
 *
 * Use of this source code is restricted per the CC BY-NC-ND license, a copy of 
 * which can be found via http://creativecommons.org (and should be included as 
 * LICENSE.txt within the associated archive or repository).
 */

#include "xlibc.h"

////////////////
// SEMAPHORES //
////////////////

bool sem_init(sem_id_t sem_id, uint32_t init) {
  bool success;

  asm volatile( "mov r0, %2 \n" //Put sem_id in r0
                "mov r1, %3 \n" //Put initial value in r1
                "svc %1     \n" // make svc call
                "mov %0, r0 \n" // assign success  = r0 
              : "=r" (success) 
              : "I" (SEM_OPEN), "r" (sem_id), "r" (init)
              : "r0", "r1" );

  return success;
}

void sem_post(sem_id_t sem_id, uint32_t x) {
  asm volatile( "mov r0, %1 \n"
                "mov r1, %2 \n"
                "svc %0     \n"
              :
              : "I" (SEM_POST), "r" (sem_id), "r" (x)
              : "r0", "r1" );
}

void sem_wait(sem_id_t sem_id, uint32_t x) {
  asm volatile( "mov r0, %1 \n"
                "mov r1, %2 \n"
                "svc %0     \n"
              :
              : "I" (SEM_WAIT), "r" (sem_id), "r" (x)
              : "r0", "r1" );
}

int  open   (char* path, char flags) {
  int fd;
  asm volatile( "mov r0, %2 \n" // Put path pointer in r0
                "mov r1, %3 \n" // Put flags in r1
                "svc %1     \n" // make svc call
                "mov %0, r0 \n" // assign result = r0 
              : "=r" (fd) 
              : "I" (OPEN), "r" (path), "r" (flags)
              : "r0", "r1" );
  return fd;
}

bool close  (int fd) {
  bool success;
  asm volatile( "mov r0, %2 \n" // Put fd in r0
                "svc %1     \n" // make svc call
                "mov %0, r0 \n" // assign success  = r0 
              : "=r" (success) 
              : "I" (CLOSE), "r" (fd)
              : "r0", "r1" );
  return success;
}

bool fd_swap(int fd1, int fd2) {
  bool success;
  asm volatile( "mov r0, %2 \n" // Put fd1 in r0
                "mov r1, %3 \n" // Put fd2 in r1
                "svc %1     \n" // make svc call
                "mov %0, r0 \n" // assign success  = r0 
              : "=r" (success) 
              : "I" (FD_SWAP), "r" (fd1), "r" (fd2)
              : "r0", "r1" );
  return success;
}

bool pipe   (int*  pfds) {
  bool success;
  asm volatile( "mov r0, %2 \n" // Put fd pointer in r0
                "svc %1     \n" // make svc call
                "mov %0, r0 \n" // assign success  = r0 
              : "=r" (success) 
              : "I" (PIPE), "r" (pfds)
              : "r0", "r1" );
  return success;
}

void execx  (void* addr, uint32_t arg) {
  asm volatile( "mov r0, %1 \n"
                "mov r1, %2 \n"
                "svc %0     \n"
              : 
              : "I" (EXECX), "r" (addr), "r" (arg)
              : "r0", "r1" );
}

bool cd     (char* path) {
  bool success;
  asm volatile( "mov r0, %2 \n" // Put path pointer in r0
                "svc %1     \n" // make svc call
                "mov %0, r0 \n" // assign success  = r0 
              : "=r" (success) 
              : "I" (CD), "r" (path)
              : "r0" );
  return success;
}

bool ls     (char* path, char* out, int nchars) {
  bool success;
  asm volatile( "mov r0, %2 \n" // Put path pointer in r0
                "mov r1, %3 \n" // Put out in r1
                "mov r2, %4 \n" // Put nchars in r2
                "svc %1     \n" // make svc call
                "mov %0, r0 \n" // assign success  = r0 
              : "=r" (success) 
              : "I" (LS), "r" (path), "r" (out), "r" (nchars)
              : "r0", "r1", "r2" );
  return success;
}

bool rm     (char* path) {
  bool success;
  asm volatile( "mov r0, %2 \n" // Put path pointer in r0
                "svc %1     \n" // make svc call
                "mov %0, r0 \n" // assign success  = r0 
              : "=r" (success) 
              : "I" (RM), "r" (path)
              : "r0" );
  return success;
}

bool isfile (char* path) {
  bool success;
  asm volatile( "mov r0, %2 \n" // Put path pointer in r0
                "svc %1     \n" // make svc call
                "mov %0, r0 \n" // assign success  = r0 
              : "=r" (success) 
              : "I" (ISFILE), "r" (path)
              : "r0" );
  return success;
}

bool isdir  (char* path) {
  bool success;
  asm volatile( "mov r0, %2 \n" // Put path pointer in r0
                "svc %1     \n" // make svc call
                "mov %0, r0 \n" // assign success  = r0 
              : "=r" (success) 
              : "I" (ISDIR), "r" (path)
              : "r0" );
  return success;
}
bool mkfile (char* path) {
  bool success;
  asm volatile( "mov r0, %2 \n" // Put path pointer in r0
                "svc %1     \n" // make svc call
                "mov %0, r0 \n" // assign success  = r0 
              : "=r" (success) 
              : "I" (MKFILE), "r" (path)
              : "r0" );
  return success;
}

bool mkdir  (char* path) {
  bool success;
  asm volatile( "mov r0, %2 \n" // Put path pointer in r0
                "svc %1     \n" // make svc call
                "mov %0, r0 \n" // assign success  = r0 
              : "=r" (success) 
              : "I" (MKDIR), "r" (path)
              : "r0" );
  return success;
}

bool getwd  (char* out, int nchars) {
  bool success;
  asm volatile( "mov r0, %2 \n" // Put out pointer in r0
                "mov r1, %3 \n" // Put nchars in r0
                "svc %1     \n" // make svc call
                "mov %0, r0 \n" // assign success  = r0 
              : "=r" (success) 
              : "I" (GETWD), "r" (out), "r" (nchars)
              : "r0", "r1" );
  return success;
}
