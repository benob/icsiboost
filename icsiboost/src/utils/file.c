#include "utils/file.h"
#include "utils/mapped.h"

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

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
	else fopen("r",filename);
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
