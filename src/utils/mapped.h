/*
 * This file is subject to the terms and conditions defined in
 * file 'COPYING', which is part of this source code package.
 */

#ifndef _MAPPED_H_
#define _MAPPED_H_

#include "utils/common.h"
#include "utils/string.h"

/**** comments :
   mmap() fails on length=0 or file size<length
   open(O_CREAT) needs attributes
*/

typedef struct mapped {
	void* data;
	int fd;
	size_t length;
	void* position;
} mapped_t;

mapped_t* mapped_new(const char* filename,size_t length);
void* mapped_resize(mapped_t* m,size_t length);
mapped_t* mapped_load(const char* filename);
mapped_t* mapped_load_readonly(const char* filename);
char* mapped_nextline(mapped_t* m);
string_t* mapped_readline(mapped_t* m);
char* mapped_gets(mapped_t* m, char* buffer, int size);
void mapped_free(mapped_t* m);

#endif
