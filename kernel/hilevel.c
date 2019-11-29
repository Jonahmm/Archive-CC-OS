/* Copyright (C) 2019 Jonah McPartlin
 *
 * Use of this source code is restricted per the CC BY-NC-ND license, a copy of 
 * which can be found via http://creativecommons.org (and should be included as 
 * LICENSE.txt within the associated archive or repository).
 */

#include "hilevel.h"

char wd[256];

// PROCESS MANAGEMENT
pcb_t pcb[PCB_SIZE];

uint32_t pcballoc   =  0;
pid_t current       = -1;

#if !SCHEDULE_AGES
int current_runtime =  0;
#endif

// SEMAPHORES
sem_t sem[PCB_SIZE] = {0};
// PIPES
pipe_t* pipes[PCB_SIZE] = {NULL};

// FILE STUFF
fs2_volume_t vol;

fdte_t* openft[64] = {NULL};

// USER PROGRAMS
extern void main_P1();
extern void main_P2();
extern void main_P3();
extern void main_P4();
extern void main_P5();
extern void main_console();
extern void safe_console();
extern void main_semtest();
extern void sh_main();

///////////////////////
// MEMORY MANAGEMENT //
///////////////////////

extern uint32_t end;
extern uint32_t _heap_end;

caddr_t brk = (caddr_t) &end;

caddr_t _sbrk( int incr ) {
  caddr_t prev = brk;
  caddr_t new = brk + incr;
  if (new > (caddr_t) &_heap_end) {
    //We've hit the heap size limit
    return (caddr_t) -1;
  }
  brk += incr;
  return prev;
}

/////////////////////
// GENERAL HELPERS //
/////////////////////

void k_print(char* in) {
  while (*in != '\0') {
    PL011_putc(UART0, *in, true);
    in++;
  }
}

void k_printn(char* in) {
  k_print(in);
  PL011_putc(UART0, '\n', true);
}

void k_print_int(int x) {
  k_print("0x");
  bool a = false;
  for (int i = 24; i >= 0; i-=8) {
    uint8_t b = (x >> i);
    a = a || (b != 0) || (i==0);
    if (a) PL011_puth(UART0, b, true);
  }
}

uint32_t top_of(stack_area_t* stack) {
  return (uint32_t) stack + sizeof(stack_area_t);
}

////////////////////////////////////////////
//   VIRTUAL FILE SYSTEM  &  FILE TABLE   //
////////////////////////////////////////////

char scratchpath[256];

char* abs_path_r(char* path, char* wd) {
  // No path given, eval to wd
  if (path == NULL || *path == '\0') return wd;
  // Path given is absolute, start from char 1
  if (*path ==  '/') return ++path;
  bool wdroot = *wd == '\0';
  if (!wdroot) {
    // Create string wd/path
    strcpy(scratchpath, wd);
    int catpnt = strlen(wd);
    scratchpath[catpnt] = '/';
    strcpy(scratchpath + catpnt + 1, path);
    return scratchpath;
  } else return path; // wd is root so path is abs
}

char* abs_path(char* path) {
  return abs_path_r(path, pcb[current].wd);
}

fmode_t fmode_from_flags(char flags) {
  flags &= 3;
  if (flags == 3) return FM_RW;
  if (flags == 2) return FM_W;
  if (flags == 1) return FM_R;
  return FM_NULL;
}


//////////////////////////////
//    PROCESS MANAGEMENT    //
//////////////////////////////

//True iff there is a process with PID given
bool process_exists(pid_t pid) {
  return ((pcballoc >> pid) & 1);
}

//Returns true iff there is a process with PID given AND its status is one of
//CREATED and READY
bool process_can_run(pid_t pid) {
  return process_exists(pid) && pcb[pid].status < STATUS_EXECUTING;
}

void context_switch(pcb_t* from, pcb_t* new, ctx_t* ctx, status_t from_stat) {
  if (from != NULL) {
    //Preserve context
    memcpy(&from->ctx, ctx, sizeof(ctx_t));
    from->status = from_stat;
  }
  memcpy(ctx, &new->ctx, sizeof(ctx_t));
  new->status = STATUS_EXECUTING;
  current = new->pid;
}

//Implements RR scheduling
void next(ctx_t* ctx, status_t cur_stat) {
  #if !SCHEDULE_AGES
  current_runtime = 0;
  #endif
  int i = (current + 1) % PCB_SIZE;
  while ( !process_can_run(i)
          && i != current     )
      i = (i + 1) % PCB_SIZE;
  
  //i is now the pcb index of the next program, or same as current if no other
  //exists
  if (i != current) {
    context_switch(&pcb[current], &pcb[i], ctx, cur_stat);
    #if PRINT_SWITCHES
    PL011_putc(UART0, '>', true);
  }
  else {
    PL011_putc(UART0, '|', true);
    #endif
  }
}

#if SCHEDULE_AGES
int aged_priority(pcb_t* p) {
  return p->base_priority + p->age;
}
//Implements priority-aging scheduling
void schedule(ctx_t* ctx) {

  int new = current;
  for (int i = (current + 1) % PCB_SIZE; i != current; i = (i + 1) % PCB_SIZE) {
    if (process_can_run(i)) { //Don't consider waiting processes
      pcb[i].age++;
      if (aged_priority(&pcb[i]) > aged_priority(&pcb[new])) new = i;
    }
  }

  if (new != current) {
    pcb[new].age = 0;
    context_switch(&pcb[current], &pcb[new], ctx, STATUS_READY);
    #if PRINT_SWITCHES
    char b = '0' + new;
    PL011_putc    (UART0,  b , true);
    PL011_putc    (UART0, ':', true);
  }
  else  {
    PL011_putc (UART0, '~', true);
    #endif
  }
}
#else
void schedule(ctx_t* ctx) {
  if (current_runtime >= pcb[current].base_priority) {
    next(ctx, STATUS_READY);
  }
  current_runtime++;
}
#endif

pid_t new_pcb_entry() {
  int i = 0;
  //Get the index of the first unallocated entry
  while (process_exists(i) && i < PCB_SIZE) i++;
  
  if (i != PCB_SIZE) pcballoc |= (1 << i);
  return i;
}

pcb_t* new_user_proc(uint32_t entry, int priority) {
  int i = new_pcb_entry();
  if (i == PCB_SIZE) //Can't launch, no available PCB space
    return NULL;

  memset( &pcb[i], 0, sizeof( pcb_t ) );     // initialise 0-th PCB = P_1
  pcb[i].pid      = i; //Use PCB index as PID
  pcb[i].status   = STATUS_CREATED;
  pcb[i].ctx.cpsr = 0x50;
  pcb[i].ctx.pc   = entry;
  memset(pcb[i].fdt, -1, 32 * sizeof(int));
  pcb[i].fdt[0] = 0;
  pcb[i].fdt[1] = 1;
  pcb[i].fdt[2] = 2;

  pcb[i].stack    = malloc(sizeof(stack_area_t));
  pcb[i].ctx.sp   = top_of(pcb[i].stack); //stack[i];

  pcb[i].base_priority = priority;
  //Age should already be set to 0 as per 7 lines ago

  return &pcb[i];
}

//////////////////
// SVC HANDLERS //
//////////////////

bool do_pipe(int * pipefds) {
  // k_print_int((int) pipefds);
  int pindex = 0;
  // 1. Get indexes for process FDs
  int wind_p, rind_p = 0; // wind_p does not need initialising here
  while (rind_p < 32 && pcb[current].fdt[rind_p] != -1) ++rind_p;
  wind_p = rind_p + 1;
  while (wind_p < 32 && pcb[current].fdt[wind_p] != -1) ++wind_p;
  if (wind_p >= 32) 
    return false; // One or both of the indexes could not be allocated.

  // 2. Get pipe index
  while (pindex < PCB_SIZE && pipes[pindex]) ++pindex;
  if(pindex==PCB_SIZE) {
    //All pipes are in use
    return false;
  }
  // 3. Get indexes for global FDs
  int rind_g = 0;
  while (rind_g < 64 && openft[rind_g] != NULL) ++rind_g;
  int wind_g = rind_g + 1;
  while (wind_g < 64 && openft[wind_g] != NULL) ++wind_g;
  if (rind_g == 64 || wind_g == 64) {
    return false;
  }
  // 4. Allocate pipe & FDs
  fdte_t *rend, *wend;
  pipes[pindex] = malloc(sizeof(pipe_t));
  // k_print_int((int) pipefds);
  if (pipes[pindex] == NULL) return false;
  rend = malloc(sizeof(fdte_t));
  if (rend == NULL) return false;
  wend = malloc(sizeof(fdte_t));
  if (wend == NULL) return false;
  // k_print_int((int) pipefds);
  // 5. Initialise the above
  pipe_reset(pipes[pindex]);
  rend->type       =    wend->type    = FT_PIPE;
  rend->id         =     wend->id     = pindex;
  rend->open_count = wend->open_count = 1;
  rend->mode = FM_R; 
  wend->mode = FM_W;
  // Use cursor var to link FD entries for easy closing
  rend->cursor = wind_g;
  wend->cursor = rind_g;

  // 6. Populate file tables
  openft[rind_g] = rend;
  openft[wind_g] = wend;
  // k_print_int((int) pipefds);
  pcb[current].fdt[rind_p] = rind_g;
  pcb[current].fdt[wind_p] = wind_g;
  // 7. Return
  pipefds[0] = rind_p;
  pipefds[1] = wind_p;
  pipefds += 0; // Without this, malloc breaks the value of pipefds. Do not ask me why.
  return true;
}

// Return the global file descriptor for the file, or -1
int do_open(char* path, char flags) {
  if (!flags) return -1; //What's the point?
  int i = 0;
  while (i < 64 && openft[i] != NULL) ++i;
  if (i == 64) return -1;

  openft[i] = malloc(sizeof(fdte_t));
  if (openft[i] == NULL) return -1;

  char* apath   = abs_path(path);
  bool  success = fs2_isftype(&vol, apath, FS2_FTYPE_FILE);
  // Could be a ||, but clearer this way
  if (!success && (flags >= 4)) success = fs2_create(&vol, FS2_FTYPE_FILE, apath); 
  if (!success) return -1;

  // File exists and can be read, fill FD
  fdte_t* fd = openft[i];
  fd->type = FT_FILE;
  fd->mode = fmode_from_flags(flags);
  // Copy path into a new mem area
  char* fdpath = malloc(strlen(apath));
  if (fdpath == NULL) return -1;
  strcpy(fdpath, apath);
  fd->id   = (uint32_t) fdpath;
  fd->cursor = 0;
  fd->open_count = 1;
  
  return i;
}

bool do_close(int fd) {
  if (fd < 0 || fd >= 32) return false;
  int i = pcb[current].fdt[fd];
  if (i == -1) return false; //Nothing to close
  
  pcb[current].fdt[fd] = -1;
  fdte_t* fde = openft[i];
  if(--fde->open_count > 0) return true;

  switch (fde->type) {
    case FT_PIPE: {
      bool close = true;
      // Close pipe, and its counterpart. Subsequent ops on the pipe will fail
      free(openft[fde->cursor]); 
    }
    case FT_UART: 
      // Just remove the process's descriptor, don't close the stream
      return true;
    case FT_FILE:
      free((char*) fde->id);
      free(fde);
      openft[i] = NULL;
      pcb[current].fdt[fd] = -1;
      return true;
    default:
      return false;
  }
}

int do_write(int fd, char* in, int nchars) {
  int i = pcb[current].fdt[fd];
  if (i == -1) return -1; //Nothing to write to
  
  fdte_t* fde = openft[i];
  if (fde==NULL) return -1;
  if (!(fde->mode == FM_W || fde->mode == FM_RW)) return 0;
  switch (fde->type) {
    case FT_UART:
      for (int j = 0; j < nchars; ++j) PL011_putc((PL011_t*) fde->id, in[j], true);
      return nchars;
    case FT_PIPE:
      return pipe_write(pipes[fde->id], in, nchars);
    case FT_FILE: {
      int n = fs2_write(&vol, (char*) fde->id, in, nchars, fde->cursor);
      if (n >= 0) fde->cursor += n;
      return n;
    }
  }
}

int do_read(int fd, char* out, int nchars) {
  int i = pcb[current].fdt[fd];
  if (i == -1) return -1; //Nothing to read from
  
  fdte_t* fde = openft[i];
  if (fde==NULL) return -1;
  if (!(fde->mode == FM_R || fde->mode == FM_RW)) return 0;
  switch (fde->type) {
    case FT_UART: {//UART reads will be treated as readline calls
      int j;
      for (j = 0; j < nchars; ++j) {
        out[j] = PL011_getc((PL011_t*) fde->id, true);
        if (out[j] = '\x0A') {
          out[j] = '\0'; break;
        }
      }
      return j;
    }
    case FT_PIPE:
      return pipe_read(pipes[fde->id], out, nchars);
    case FT_FILE: {
      int n = fs2_read(&vol, (char*) fde->id, out, nchars, fde->cursor);
      if (n >= 0) fde->cursor += n;
      return n;
    }
  }
}

bool do_cd(char* cd) {
  char* apath = abs_path(cd);
  if (!fs2_isftype(&vol, apath, FS2_FTYPE_DIR)) return false;
  strcpy(pcb[current].wd, apath);
  return true;
}

void halt() {
  int_unable_irq();
  k_print("\nHALT\n");
  //HALT
  while (1);
}

void do_exit() {
  #if PRINT_SWITCHES
    PL011_putc(UART0, '*', true);
  #endif
  pcb[current].status = STATUS_TERMINATED;
  pcb[current].base_priority = -1;
  free(pcb[current].stack);
  for(int i = 0; i < 32; ++i) {
    if (pcb[current].fdt[i] != -1) do_close(i);
  }
  //Age will be 0 as has been executing
  pcballoc -= (1 << current);
  /////////////////////////////////////////////////////////
  // IF ALL PROCESSES TERMINATED KERNEL SHOULD HALT HERE //
  /////////////////////////////////////////////////////////
  if(!pcballoc) halt();
}

//Create a new process identical to the current, differentiating between parent
//and child
void do_fork(ctx_t* ctx) {
  //Preserve context
  memcpy(&pcb[current].ctx, ctx, sizeof(ctx_t));
  pid_t child_pid = new_pcb_entry();
  if (child_pid == PCB_SIZE) {
    //PCB table is full
    PL011_putc(UART0, '!', true);
    ctx->gpr[0] = -1;
    return;
  }
  #if PRINT_SWITCHES
    PL011_putc(UART0, 'f', true);
  #endif
  //Init child, with same priority as parent
  memcpy(& pcb [child_pid], & pcb [current], sizeof(pcb_t));

  stack_area_t* child_stack = malloc(sizeof(stack_area_t));
  if (child_stack == NULL) {
    //Could not allocate memory for the child process's stack
    PL011_putc(UART0, 'M', true);
    ctx->gpr[0] = -2;
    return;
  }
  memcpy(child_stack, pcb[current].stack, sizeof(stack_area_t));

  pcb[child_pid].pid    = child_pid;
  pcb[child_pid].status = STATUS_CREATED;

  // Correct stack pointer: without the below casts the subtraction returns an
  // incorrect value
  uint32_t s_cur = (uint32_t) pcb[current].stack;
  uint32_t s_cld = (uint32_t) child_stack;
  pcb[child_pid].ctx.sp += (s_cld - s_cur);

  pcb[child_pid].stack   = child_stack;

  // Differentiate processes
  pcb[child_pid].ctx.gpr[0] = 0;
  ctx->gpr[0] = child_pid;

  // Update file descriptors
  for(int i = 0; i < 32; ++i) {
    if (pcb[current].fdt[i] != -1) openft[pcb[current].fdt[i]]->open_count++;
  }
}

void do_exec(ctx_t* ctx) {
  uint32_t entry = ctx->gpr[0];
  ctx->pc   = entry;
  ctx->sp   = top_of(pcb[current].stack);
  ctx->cpsr = 0x50;
}

void do_kill(pid_t pid) {
  if (pid < 0 || pid >= PCB_SIZE) return;
  if (process_exists(pid)) {
    #if PRINT_SWITCHES
      PL011_putc(UART0, 'k', true);
    #endif
    pcballoc -= (1 << pid);
    free(pcb[pid].stack);
  } 

  if(!pcballoc) halt();
}

//Check for any process P that is waiting and eligible for the semaphore given
//by sem_id, if P exists increment the sem value by x-y (where y is the amount
//P was waiting for) and set P to active. Otherwise, increment sem value by x.
//A potential flaw with this system may manifest when >1 processes are waiting
//for different quantities of the semaphore. The process waiting for the
//higher value may starve if another process or processes loop(s), posting and
//waiting for the same, lower value.
void do_sem_post(sem_id_t sem_id, uint32_t x) {
  //Return if id out of range, or x is 0 (no effect)
  if (sem_id < 0 || sem_id >= PCB_SIZE || !x) return; 
  #if PRINT_SEM_OPS
    PL011_putc(UART0, '[', true);
    PL011_putc(UART0, 'p', true);
    PL011_putc(UART0, '0' + sem_id, true);
    PL011_putc(UART0, '=', true);
  #endif
  for ( int i = (current + 1) % PCB_SIZE;
        i != current;
        i = (i + 1) % PCB_SIZE ) {
    if (process_exists(i)
        && pcb[i].status == STATUS_WAITING
        && pcb[i].waiting->sem_id == sem_id
        && pcb[i].waiting->x      <= x     ) {
          //Process i is waiting and eligible for this semaphore
          sem[sem_id] += (x - pcb[i].waiting->x); //Increase sem by x-y
          pcb[i].status = STATUS_READY; //Set process i to active
          free(pcb[i].waiting); //Deallocate the wait values
          // pcb[i].waiting = NULL; //Not required
          #if PRINT_SEM_OPS
            PL011_putc(UART0, '(', true);
            PL011_putc(UART0, 'w', true);
            PL011_putc(UART0, '0' + i, true);
            PL011_putc(UART0, ')', true);
            PL011_putc(UART0, '0' + sem[sem_id], true);
            PL011_putc(UART0, ']', true);
          #endif
          return;
        }
  }
  //If this statement is reached, no process was waiting and eligible for sem,
  //so simply increase it.
  sem[sem_id] += x;
  #if PRINT_SEM_OPS
    PL011_putc(UART0, '0' + sem[sem_id], true);
    PL011_putc(UART0, ']', true);
  #endif
}

//If the semaphore indicated by sem_id has ≥x units, decrement it and return
//control to the current process. If not, set the current process to WAITING
//and yield control.
bool do_sem_wait(sem_id_t sem_id, uint32_t x) {
  if (sem_id < 0 || sem_id >= PCB_SIZE) { 
    //Unsure what to do in this case: probably terminate the process
    k_print("\nProcess 0x");
    if (current > 15) PL011_putc(UART0, '1', true);
    PL011_puth(UART0, current, true);
    k_print(" attempted to wait for invalid semaphore - terminating.\n");
    do_exit();
  }
  #if PRINT_SEM_OPS
    PL011_putc(UART0, '[', true);
    PL011_putc(UART0, 'w', true);
    PL011_putc(UART0, '0' + sem_id, true);
    PL011_putc(UART0, '=', true);
  #endif
  if (sem[sem_id] >= x) {
    sem[sem_id] -= x;
    #if PRINT_SEM_OPS
      PL011_putc(UART0, '0' + sem[sem_id], true);
      PL011_putc(UART0, ']', true);
    #endif
    return true;
  }
  pcb[current].status  = STATUS_WAITING;
  pcb[current].waiting = malloc(sizeof(semwait_t));
  pcb[current].waiting->sem_id = sem_id;
  pcb[current].waiting->x = x;
  #if PRINT_SEM_OPS
    PL011_putc(UART0, 'W', true);
    PL011_putc(UART0, ']', true);
  #endif
  return false;
}

void init_timer() {
  /* Configure the mechanism for interrupt handling by
   *
   * - configuring timer st. it raises a (periodic) interrupt for each 
   *   timer tick,
   * - configuring GIC st. the selected interrupts are forwarded to the 
   *   processor via the IRQ interrupt signal, then
   * - enabling IRQ interrupts.
   */

  TIMER0->Timer1Load  =  INTERVAL ; // select period
  TIMER0->Timer1Ctrl  = 0x00000002; // select 32-bit   timer
  TIMER0->Timer1Ctrl |= 0x00000040; // select periodic timer
  TIMER0->Timer1Ctrl |= 0x00000020; // enable          timer interrupt
  TIMER0->Timer1Ctrl |= 0x00000080; // enable          timer

  GICC0->PMR          = 0x000000F0; // unmask all            interrupts
  GICD0->ISENABLER1  |= 0x00000010; // enable timer          interrupt
  GICC0->CTLR         = 0x00000001; // enable GIC interface
  GICD0->CTLR         = 0x00000001; // enable GIC distributor

  int_enable_irq();
}

void init_fs() {
  k_print("Boot: Loading file system… ");
  vol.blk_0 = 0;
  vol.outcome = FS2_SUCCESS;
  fs2_load_volume(&vol);
  *wd = '\0';
  if (vol.outcome == FS2_SUCCESS) {
    k_print("success!\nLoaded CWFS2 volume at block 0:\n  ");
    k_print_int(vol.hblock.nblocks   ); k_print(" blocks.\n  ");
    k_print_int(vol.hblock.next_block); k_print(" blocks used\n");
    k_print_int(vol.hblock.next_inode);
    k_print(" files.\n  Block dump:\n    ");
    char x[65];
    fs2_block_dump(&vol, x, 64); x[64] = '\0';
    k_printn(x);
  }
  else {
    k_print("fail.\nFormatting volume… ");
    fs2_format_volume(&vol, 100, FS2_U_READ | FS2_U_WRITE, 4, 1, 2);
    k_print(vol.outcome == FS2_SUCCESS ? "success!\n" : "fail.\n");
  }
  // Init STDIN, STDOUT, STDERR
  fdte_t* fd = malloc(sizeof(fdte_t));
  fd->type = FT_UART;
  fd->id   = (uint32_t) UART0;
  fd->mode = FM_R;
  openft[0] = fd;

  fd = malloc(sizeof(fdte_t));
  fd->type = FT_UART;
  fd->id   = (uint32_t) UART0;
  fd->mode = FM_W;
  openft[1] = fd;

  fd = malloc(sizeof(fdte_t));
  fd->type = FT_UART;
  fd->id   = (uint32_t) UART0;
  fd->mode = FM_W;
  openft[2] = fd;
}

//////////////////////////////
//    INTERRUPT HANDLERS    //
//////////////////////////////

void hilevel_handler_rst(ctx_t* ctx) {
  init_fs();
  k_print("Boot: Loading boot programs\n");  
  pcb_t* p1 = new_user_proc(( uint32_t ) INIT_PROGRAM, 5);

  k_print("Boot: Launching shell\n-------- Boot Complete --------\n");
  context_switch(NULL, p1, ctx, STATUS_CREATED);

  init_timer();
  return;
}

void hilevel_handler_irq(ctx_t* ctx) {
  uint32_t id = GICC0->IAR;

  if( id == GIC_SOURCE_TIMER0 ) {
    schedule(ctx);
    TIMER0->Timer1IntClr = 0x01;
  }

  GICC0->EOIR = id;

  return;
}

void hilevel_handler_svc(ctx_t* ctx, uint32_t id) {
  // int_unable_irq();
  switch (id)
  {
    case 0: //YIELD
        #if PRINT_SWITCHES
        PL011_putc(UART0, 'y', true);
        #endif
        next(ctx, STATUS_READY);
        break;
    case 1: { // WRITE 
        int   fd =  (int)  ( ctx->gpr[ 0 ] );  
        char* in = (char*) ( ctx->gpr[ 1 ] );  
        int    n =  (int)  ( ctx->gpr[ 2 ] ); 
        
        ctx->gpr[0] = do_write(fd, in, n);
        break;
    }
    case 2: { // READ 
        int    fd =  (int)  ( ctx->gpr[ 0 ] );  
        char* out = (char*) ( ctx->gpr[ 1 ] );  
        int     n =  (int)  ( ctx->gpr[ 2 ] ); 
        
        ctx->gpr[0] = do_read(fd, out, n);
        break;
    }
    case 3: //FORK
        do_fork(ctx);
        break;
    case 4: //EXIT
        do_exit();
        next(ctx, STATUS_TERMINATED);
        break;
    case 5: //EXEC
        do_exec(ctx);
        break;
    case 6: //KILL
        do_kill(ctx->gpr[0]);
        break;
    case 7: { //NICE
        pid_t pid  = (pid_t) ctx->gpr[0];
        int   newp =  (int)  ctx->gpr[1];
        if (pid < 0 || pid >= PCB_SIZE || newp < 0) break;
        pcb[pid].base_priority = newp;
        break;
    }
    case 8: { //SEM_INIT
        //Set semaphore sem_id to value init
        //To avoid deadlock, use sem_post to do this as it will wake processes
        //that are waiting for this semaphore.
        sem_id_t sem_id = ctx->gpr[0];
        uint32_t  init  = ctx->gpr[1];
        if (sem_id < 0 || init >= PCB_SIZE) {
          ctx->gpr[0] = false;
          break;
        }
        //Post by (initial value - current) to set to initial value and wake 
        do_sem_post(sem_id, (init - sem[sem_id]));
        //Op has succeeded
        ctx->gpr[0] = true;
        break;
    }
    case 9: { //SEM_POST
        sem_id_t sem_id = ctx->gpr[0];
        uint32_t    x   = ctx->gpr[1];
        do_sem_post(sem_id, x);
        break;
    }
    case 0xA: { //SEM_WAIT
        sem_id_t sem_id = ctx->gpr[0];
        uint32_t    x   = ctx->gpr[1];
        if (do_sem_wait(sem_id, x)) 
          //Did not have to wait for semaphore
          break;
        next(ctx, STATUS_WAITING);
        break;
    }
    case 0x0B: { // OPEN
      int i = 0;
      while (i < 32 && pcb[current].fdt[i] != -1) ++i;
      if (i == 32) {
        ctx->gpr[0] = -1;
        break;
      }
      int f = do_open((char*) ctx->gpr[0], (char) ctx->gpr[1]);
      if (f != -1) {
        pcb[current].fdt[i] = f;
        ctx->gpr[0] = i;
      } else ctx->gpr[0] = -1;
      break;
    }
    case 0x0C: { //CLOSE
      ctx->gpr[0] = do_close(ctx->gpr[0]);
      break;
    }
    case 0x0D: { //FD_SWAP
      // Swap even if invalid
      int fd1 = ctx->gpr[0];
      int fd2 = ctx->gpr[1];
      if (fd1 < 0 || fd2< 0 || fd1>=32 || fd2>=32) 
        {ctx->gpr[0] = false; break;}
      int t = pcb[current].fdt[fd1];
      pcb[current].fdt[fd1] = pcb[current].fdt[fd2];
      pcb[current].fdt[fd2] = t;
      ctx->gpr[0] = true;
      break;
    }
    case 0x0E: { // PIPE
        int* pipefds = (int*) ctx->gpr[0];
        do_pipe(pipefds);
        break;
    }
    case 0x0F: { // EXECX
      do_exec(ctx);
      // r1 will not have been affected by exec, but its value needs to be in r0
      ctx->gpr[0] = ctx->gpr[1];
      break;
    }
    case 0x10: { // ISFILE
      ctx->gpr[0] = fs2_isftype(&vol, abs_path((char*) ctx->gpr[0]), FS2_FTYPE_FILE);
      break;
    }
    case 0x11: { // ISDIR
      ctx->gpr[0] = fs2_isftype(&vol, abs_path((char*) ctx->gpr[0]),  FS2_FTYPE_DIR);
      break;
    }
    case 0x12: { // CD
      ctx->gpr[0] = do_cd((char*) ctx->gpr[0]);
      break;
    }
    case 0x13: { // LS
      char* path = abs_path((char*) ctx->gpr[0]);
      ctx->gpr[0] = fs2_ls(&vol, path, (char*) ctx->gpr[1], (int) ctx->gpr[2]);
      break;
    }
    case 0x14: { // RM
      ctx->gpr[0] = fs2_rm(&vol, abs_path((char*) ctx->gpr[0]));
      break;
    }
    case 0x15: { // MKFILE
      ctx->gpr[0] = fs2_create(&vol, FS2_FTYPE_FILE, abs_path((char*) ctx->gpr[0]));
      break;
    }
    case 0x16: { // MKDIR
      ctx->gpr[0] = fs2_create(&vol, FS2_FTYPE_DIR,  abs_path((char*) ctx->gpr[0]));
      break;
    }
    case 0x18: { // GETWD
      strncpy((char*) ctx->gpr[0], pcb[current].wd, (size_t) ctx->gpr[1]);
      ctx->gpr[0] = true;
      break;
    }
    default: // CHMOD will fall through as it is not implemented.
      break;
  }
  return;
}
