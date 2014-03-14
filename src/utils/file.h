/*
 * This file is subject to the terms and conditions defined in
 * file 'COPYING', which is part of this source code package.
 */

#ifndef _FILE_H_
#define _FILE_H_

#include "utils/common.h"
#include "utils/string.h"
#include "utils/array.h"

#define FILE_NONE '\0'
#define FILE_READABLE 'r'
#define FILE_WRITABLE 'w'
#define FILE_EXECUTABLE 'x'
#define FILE_OWNED 'o'

#define FILE_READABLE_REAL_ID 'R'
#define FILE_WRITABLE_REAL_ID 'W'
#define FILE_EXECUTABLE_REAL_ID 'X'
#define FILE_OWNED_REAL_ID 'O'

#define FILE_EXISTS 'e'
#define FILE_EMPTY 'z'
#define FILE_SIZE 's'

#define FILE_PLAIN 'f'
#define FILE_DIRECTORY 'd'
#define FILE_LINK 'l'
#define FILE_PIPE 'p'
#define FILE_SOCKET 'S'
#define FILE_BLOCK_DEVICE 'b'
#define FILE_CHARACTER_DEVICE 'c'
#define FILE_TTY 't'

#define FILE_SETUID 'u'
#define FILE_SETGID 'g'
#define FILE_STICKY 'k'

#define FILE_TEXT 'T'
#define FILE_BINARY 'B'

#define FILE_MODIFIED_SECONDS 'M'
#define FILE_ACCESSED_SECONDS 'A'
#define FILE_CHANGED_SECONDS 'C'

int file_test(const char* name, const char* flags);

/*#define FILE_MODE_READ 1
#define FILE_MODE_WRITE 2
#define FILE_MODE_APPEND 3
#define FILE_MODE_EXEC_FROM 4
#define FILE_MODE_EXEC_TO 5
FILE* file_new(const char* name, int mode);
void file_free(FILE* fp);
*/

array_t* file_readlines(const char* filename);
int file_writelines(const char* filename, array_t* lines);
#ifdef HAVE_LIBZ
array_t* file_gz_readlines(const char* filename);
#endif
string_t* file_readline(FILE* file);

size_t file_num_lines(const char* filename);

#endif
