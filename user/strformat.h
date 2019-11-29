/* Copyright (C) 2019 Jonah McPartlin
 *
 * Use of this source code is restricted per the CC BY-NC-ND license, a copy of 
 * which can be found via http://creativecommons.org (and should be included as 
 * LICENSE.txt within the associated archive or repository).
 */

#include "libc.h"
#include "string.h"

char* format_hex(char* string, int vals[]);

void print_hex(char* string, int nchars, int vals[]);

void printn(char* string, int nchars);