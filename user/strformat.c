/* Copyright (C) 2019 Jonah McPartlin
 *
 * Use of this source code is restricted per the CC BY-NC-ND license, a copy of 
 * which can be found via http://creativecommons.org (and should be included as 
 * LICENSE.txt within the associated archive or repository).
 */

#include "strformat.h"

char hex_char(int value) {
    value &= 0x0F;
    return value < 10 ? ('0' + value) : ('A' + value - 10);
}

void hex(char* segment, int length, int value) {
    for (int i = 0; i < length; i++) {
        segment[i] = hex_char(value >> (4 * (length - 1 - i)));
    }
}

//Given a pointer to the start of a string and an array of integer values to
//format as hex, replace the ith contiguous region of '@' characters with the
//hex representation of the ith value. Returns the given pointer so it be used
//inline.
char* format_hex(char* string, int* vals) {
    //eg "abc %%:%%", [23, 1] = "abc 17:01"
    int val = 0;
    for (int i = 0; (string[i] != '\0'); i++) {
        int j = i;
        while (string[j] == '@') j++;
        if (j!=i) {
            hex(&string[i], j-i, vals[val]);
            val++;
            //Would do i = j + 1 but this could result in skipping the end of
            //the string
            i = j;
        }
    }
    return string;
}

void print_hex(char* string, int nchars, int* vals) {
    char x[nchars];
    strcpy(x, string);
    write(STDOUT_FILENO, format_hex(x, vals), nchars);
}

void printn(char* string, int nchars) {
    write(STDOUT_FILENO, string, nchars);
}