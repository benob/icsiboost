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

#include "utils/file.h"
#include "utils/mapped.h"

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <zlib.h>

int file_test(const char* name, const char* flags)
{
	struct stat stat_result;
	if(stat(name, &stat_result)==-1)
	{
		if(flags[0]!=FILE_EXISTS)warn("file_test(\"%s\",\"%s\") failed", name, flags);
		return 0;
	}
	if(flags==NULL || flags[0]=='\0' || flags[1]!='\0')
	{
		warn("file_test(\"%s\",\"%s\"), invalid flags", name, flags);
	}
	time_t now=time(NULL);
	switch (flags[0]) {
	case FILE_READABLE:break;
		//return stat_result.st_mode & S_IRUSR;
	case FILE_WRITABLE:break;
		//return stat_result.st_mode & S_IWUSR;
	case FILE_EXECUTABLE:break;
		//return stat_result.st_mode & S_IXUSR;
	case FILE_OWNED:break;
		return stat_result.st_uid==geteuid();

	case FILE_READABLE_REAL_ID:break;
	case FILE_WRITABLE_REAL_ID:break;
	case FILE_EXECUTABLE_REAL_ID:break;
	case FILE_OWNED_REAL_ID:
		return stat_result.st_uid==getuid();

	case FILE_EXISTS:
		return 1;
	case FILE_EMPTY:
		return stat_result.st_size==0;
	case FILE_SIZE:
		return stat_result.st_size;

	case FILE_PLAIN:
		return S_ISREG(stat_result.st_mode);
	case FILE_DIRECTORY:
		return S_ISDIR(stat_result.st_mode);
	case FILE_LINK:
		return S_ISLNK(stat_result.st_mode);
	case FILE_PIPE:break;
	case FILE_SOCKET:
		return S_ISSOCK(stat_result.st_mode);
	case FILE_BLOCK_DEVICE:
		return S_ISBLK(stat_result.st_mode);
	case FILE_CHARACTER_DEVICE:
		return S_ISCHR(stat_result.st_mode);
	case FILE_TTY:break;

	case FILE_SETUID:
		return stat_result.st_mode & S_ISUID;
	case FILE_SETGID:
		return stat_result.st_mode & S_ISGID;
	case FILE_STICKY:
		return stat_result.st_mode & S_ISVTX;

	case FILE_TEXT:break;
	case FILE_BINARY:break;

	case FILE_MODIFIED_SECONDS:
		return now-stat_result.st_mtime;
	case FILE_ACCESSED_SECONDS:
		return now-stat_result.st_atime;
	case FILE_CHANGED_SECONDS:
		return now-stat_result.st_ctime;
	default:
		warn("unknown file test \"%s\"",flags);
		return 0;
	}
	warn("file_test(\"%s\",\"%s\") not implemented yet", name, flags);
	return 0;
}

array_t* file_readlines(const char* filename)
{
	FILE* file_pointer=NULL;
	if(strcmp(filename,"-")==0) file_pointer=stdin;
	else file_pointer=fopen(filename,"r");
	if(file_pointer==NULL)
	{
		warn("file_readlines(\"%s\") failed", filename);
		return NULL;
	}
	array_t* output=array_new();
	string_t* line=NULL;
	while((line=string_readline(file_pointer))!=NULL)
	{
		array_push(output, line);
	}
	fclose(file_pointer);
	return output;
}

int file_writelines(const char* filename, array_t* lines)
{
	FILE* file_pointer=NULL;
	if(strcmp(filename,"-")==0) file_pointer=stdout;
	else file_pointer=fopen(filename,"w");
	if(file_pointer==NULL)
	{
		warn("file_writelines(\"%s\", %p) failed", filename, lines);
		return 0;
	}
	int i;
	for(i=0; i<lines->length; i++)
	{
		string_t* line=array_get(lines, i);
		fprintf(file_pointer,"%s", line->data);
	}
	if(file_pointer!=stdout)fclose(file_pointer);
	return 1;
}

array_t* file_gz_readlines(const char* filename)
{
	gzFile* file_pointer=NULL;
	if(strcmp(filename,"-")==0) file_pointer=gzdopen(0,"r");
	else file_pointer=gzopen(filename,"r");
	if(file_pointer==NULL)
	{
		warn("file_readlines(\"%s\") failed", filename);
		return NULL;
	}
	array_t* output=array_new();
	char buffer[4096];
	while((gzgets(file_pointer, buffer, 4095))!=NULL)
	{
		string_t* line=string_new(buffer);
		array_push(output, line);
	}
	gzclose(file_pointer);
	return output;
}

string_t* file_readline(FILE* file_pointer)
{
	return string_readline(file_pointer);
}

size_t file_num_lines(const char* filename)
{
	mapped_t* mapped=mapped_load_readonly(filename);
	if(mapped==NULL)
	{
		warn("file_num_lines(%s), failed", filename);
		return 0;
	}
	char* data=mapped->data;
	size_t i;
	size_t counter=0;
	for(i=0;i<mapped->length;i++)
	{
		if(data[i]=='\n')counter++;
	}
	mapped_free(mapped);
	return counter;
}
