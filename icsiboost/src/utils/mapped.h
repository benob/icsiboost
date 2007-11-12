/* Copyright (C) (2007) (Benoit Favre) <favre@icsi.berkeley.edu>

This program is free software; you can redistribute it and/or 
modify it under the terms of the GNU Lesser General Public License 
as published by the Free Software Foundation; either 
version 2 of the License, or (at your option) any later 
version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU Lesser General Public License for more details.

You should have received a copy of the GNU Lesser General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA. */

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
