/* Copyright (C) 2017 Daniel Page <csdsp@bristol.ac.uk>
 *
 * Use of this source code is restricted per the CC BY-NC-ND license, a copy of 
 * which can be found via http://creativecommons.org (and should be included as 
 * LICENSE.txt within the associated archive or repository).
 */

SECTIONS {
  /* assign load address (per  QEMU) */
  .       =     0x70010000; 
  /* place text segment(s)           */
  .text : { kernel/lolevel.o(.text) *(.text .rodata) }
  /* place data segment(s)           */        
  .data : {                         *(.data        ) }
  /* place bss  segment(s)           */        
  .bss  : {                         *(.bss         ) }
  . = ALIGN( 8 );
  .heap : {
    end = .;
    _heap_start = .;
    . = . + 0x01000000;
    _heap_end = .;
  }
  /* align       address (per AAPCS) */
  .       = ALIGN( 8 );        
  /* allocate stack for irq mode     */
  .       = . + 0x00001000;  
  tos_irq = .;
  /* allocate stack for svc mode     */
  .       = . + 0x00001000;  
  tos_svc = .;
  /* allocate stack(s) for usr programs 
  .       = . + 0x00001000;  
  tos_u0  = .;
  .       = . + 0x00001000;  
  tos_u1  = .;
  .       = . + 0x00001000;  
  tos_u2  = .; */
}
