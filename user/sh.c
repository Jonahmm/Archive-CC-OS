/* Copyright (C) 2019 Jonah McPartlin
 *
 * Use of this source code is restricted per the CC BY-NC-ND license, a copy of 
 * which can be found via http://creativecommons.org (and should be included as 
 * LICENSE.txt within the associated archive or repository).
 */

// A limited (though less so than console) shell
// As with console, all IO will be done directly through the UART, without the 
// need for syscalls.
#include "PL011.h"
#include "libc.h"
#include "xlibc.h"
#include "string.h"

// -- adapted from console.c --- +
void xputs( char* x, int n ) {
  for( int i = 0; i < n; i++ ) {
    PL011_putc( UART1, x[ i ], true );
  }
}

int  xgets( char* x, int n ) {
  for( int i = 0; i < n; i++ ) {
    x[ i ] = PL011_getc( UART1, true );
    
    if( x[ i ] == '\x0A' ) {
      x[ i ] = '\x00';
      return i;
    }
  }
}
// ----------------------------- +




void rep_op(bool outcome) {
  xputs(outcome ? "Success\n" : "Failure\n", 8);
}

bool xtool(char* cmd) {
    if (!strcmp(cmd, "exit")) {
        exit(EXIT_SUCCESS);
        // No need to return
    }
    strtok(cmd, " ");
    if ( 0 == strcmp(cmd, "cd"     )) {
        rep_op( cd    ( strtok(NULL, " ")));
        return true;
    }
    if ( 0 == strcmp(cmd, "rm"     )) {
        rep_op( rm    ( strtok(NULL, " ")));
        return true;
    }
    if ( 0 == strcmp(cmd, "mkfile" )) {
        rep_op( mkfile( strtok(NULL, " ")));
        return true;
    }
    if ( 0 == strcmp(cmd, "mkdir"  )) {
        rep_op( mkdir ( strtok(NULL, " ")));
        return true;
    }
    if ( 0 == strcmp(cmd, "isfile" )) {
        rep_op( isfile( strtok(NULL, " ")));
        return true;
    }
    if ( 0 == strcmp(cmd, "isdir"  )) {
        rep_op( isdir ( strtok(NULL, " ")));
        return true;
    }
    if ( 0 == strcmp(cmd, "ls"  )) {
        char out[256];
        bool s = ls(strtok(NULL, " "), out, 256);
        if (s) {
            for (int i = 0; i < 256 && out[i] != '\0'; ++i) {
            PL011_putc(UART1, out[i], true);
            }
        } else xputs("Failure\n", 8);
        return true;
    }
    if ( 0 == strcmp(cmd, "getwd"  )) {
        char out[256];
        bool s = getwd(out, 256);
        if (s) {
            for (int i = 0; i < 256 && out[i] != '\0'; ++i) {
            PL011_putc(UART1, out[i], true);
            }
        } else xputs("Failure\n", 8);
        return true;
    }
    if ( 0 == strcmp(cmd, "kill")) {
        pid_t pid = atoi(strtok(NULL, " "));
        int    s  = atoi(strtok(NULL, " "));
        kill(pid, s);
        return true;
    }
    if ( 0 == strcmp(cmd, "setp")) {
        pid_t pid = atoi(strtok(NULL, " "));
        int    s  = atoi(strtok(NULL, " "));
        nice(pid, s);
        return true;
    }
    return false;
}

extern void main_P3(); 
extern void main_P4(); 
extern void main_P5();
extern void main_semtest();
extern void phil_spawner();
extern void main_P1();
extern void main_P2();
extern void pipe_test();
extern void cat(char*);
extern void wc(char*);

void* xload(char* cmd) {
    if (strcmp(cmd, "cat") == 0) return &cat;
    if (strcmp(cmd, "wc") == 0) return &wc;
    if (strcmp(cmd, "P3") == 0) return &main_P3;
    if (strcmp(cmd, "P4") == 0) return &main_P4;
    if (strcmp(cmd, "P5") == 0) return &main_P5;
    if( 0 == strcmp(cmd, "phil" )) return &phil_spawner;
    if( 0 == strcmp(cmd, "sem"  )) return &main_semtest;
    if( 0 == strcmp(cmd, "P1"   )) return &main_P1;
    if( 0 == strcmp(cmd, "P2"   )) return &main_P2;
    return NULL;
}

void xaction(char*cmd, char* arg, char* out) {
    void* addr = xload(cmd);
    if (addr == NULL) {
        xputs("Unrecognised command.\n", 22);
        return;
    }
    int pid = fork();
    if (pid == 0) {
        if(*out != '\0') {
            int f = open(out, F_WRITE | F_CREATE);
            if (f == -1) exit(EXIT_FAILURE);
            // This means that all writes to SDTOUT will now go to file, as
            // expected.
            fd_swap(STDOUT_FILENO, f);
        }
        if (*arg == '\0') exec(addr);
        else execx(addr, (uint32_t) arg);
    }
}

void sh_main() {
    char *p1;
    char *arg;
    char *out;
    char x[1024];
    int lim;

    while (1) {
        xputs("xsh$ ", 5);
        lim = xgets(x, 1024);

        if(!xtool(x)) {
            xputs(" Args: ", 7);
            arg = x + lim + 1;
            lim += xgets(arg, 1024 - lim);
            xputs(" Write to: ", 11);
            out = x + lim + 1;
            xgets(out, 1024 - lim);
            xaction(x, arg, out);
        }
    }
}