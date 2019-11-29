/* Copyright (C) 2019 Jonah McPartlin
 *
 * Use of this source code is restricted per the CC BY-NC-ND license, a copy of 
 * which can be found via http://creativecommons.org (and should be included as 
 * LICENSE.txt within the associated archive or repository).
 */

#ifndef __HILEVEL_H
#define __HILEVEL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/types.h>
#include <string.h>

#include "lolevel.h"
#include     "int.h"

#include "PL011.h"
#include "GIC.h"
#include "SP804.h"

#include "pipe.h"
#include "file.h"
#include "fs2.h"

#endif


#define TM_FAST 0x00001000
#define TM_MEDI 0x00008000
#define TM_SLOW 0x00010000

#define STDIN  0
#define STDOUT 1
#define STDERR 2

// MODIFIABLE KERNEL PROPERTIES
#define PCB_SIZE (0x20) //Defined as such so that we can use a single variable
                        //to keep track of allocations
#define STACK_SIZE (0x400)

// If true, the scheduler will use ages
#define SCHEDULE_AGES false

#define PRINT_SWITCHES false
#define PRINT_SEM_OPS  false
#define PRINT_FILE_OPS true

#define INTERVAL TM_FAST
#define INIT_PROGRAM &sh_main

typedef int pid_t;
typedef uint32_t sem_t;
typedef uint32_t sem_id_t;

typedef enum { 
  STATUS_CREATED,
  STATUS_READY,
  STATUS_EXECUTING,
  STATUS_WAITING,
  STATUS_TERMINATED
} status_t;

typedef struct {
  uint32_t cpsr, pc, gpr[ 13 ], sp, lr;
} ctx_t;

typedef struct {
  uint32_t data[STACK_SIZE];
} stack_area_t;

/////
//// IPC
///
//Used when waiting for a semaphore
typedef struct {
  //Which semaphore we wait for
  sem_id_t  sem_id;
  //How much we need from it to resume execution
  uint32_t  x;    
} semwait_t;


//////
/////  PCB ENTRIES
////
typedef struct {
  pid_t    pid;
  status_t status;
  int base_priority;
#if SCHEDULE_AGES
  int age;
#endif
  //Keep track of which (if any) semaphore this process is waiting for, 
  //and the quantity it needs from it
  semwait_t*    waiting;
  //Point to the base of the 4KiB area in memory assigned to this process's stack
  stack_area_t* stack;
  // Reference fdtes in the global fdt
  int      fdt[32];
  char     wd [256];
  ctx_t    ctx;
} pcb_t;
