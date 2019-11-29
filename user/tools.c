/* Copyright (C) 2019 Jonah McPartlin
 *
 * Use of this source code is restricted per the CC BY-NC-ND license, a copy of 
 * which can be found via http://creativecommons.org (and should be included as 
 * LICENSE.txt within the associated archive or repository).
 */

#include "libc.h"
#include "xlibc.h"
#include "strformat.h"

// cat: if a path is given, read the whole file and redirect to stdout
void cat(char* path) {
    char buf[1024];
    int n;
    int f = open(path, F_READ);
    if (f == -1) {
        printn("File could not be opened.", 25);
        exit(EXIT_FAILURE);
    }
    while (1) {
        n = read(f, buf, 1024);
        if (n) write(STDOUT_FILENO, buf, n);
        else {
            close(f);
            exit(EXIT_SUCCESS);
        }
    }
}

// wc: Output total words and characters
void wc(char* path) {
    char buf[1024];
    int count[2];
    count[0] = 0; count[1] = 0;
    int n;
    int f = open(path, F_READ);
    if (f == -1) exit(EXIT_FAILURE);
    while (1) {
        n = read(f, buf, 1024);
        if (n) {
            count[1] += n;
            for (int i = 0; i < n; ++i) 
                if (buf[i] == ' ' || buf[i] == '\n')
                    ++count[0];
        }
        else {
            close(f);
            print_hex("Counted 0x@@@@ words in 0x@@@@ total characters.\n", 49, count);
            exit(EXIT_SUCCESS);
        }
    }
}