/*
 * This file is subject to the terms and conditions defined in
 * file 'COPYING', which is part of this source code package.
 */

#include "mapped.h"

#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

mapped_t* mapped_new(const char* filename,size_t length)
{
	if(length==0)length=1;
	mapped_t* m=MALLOC(sizeof(mapped_t));
	if(m==NULL)return NULL;
	m->fd=open(filename,O_CREAT|O_RDWR|O_TRUNC,0644);
	if(m->fd==-1)
	{
		warn("open(%s)",filename);
		FREE(m);
		return NULL;
	}
	lseek(m->fd,length-1,SEEK_SET);
	int c=0;
	write(m->fd,&c,1);
	m->data=mmap(NULL,length,PROT_READ|PROT_WRITE,MAP_SHARED,m->fd,0L);
	if(m->data==MAP_FAILED)
	{
		warn("mmap(%s,%d)",filename,(int)length);
		FREE(m);
		return NULL;
	}
	m->position=m->data;
	m->length=length;
	return m;
}

void* mapped_resize(mapped_t* m,size_t length)
{
	if(length==m->length)return m->data;
	munmap(m->data,m->length);
	if(m->data==MAP_FAILED)
	{
		warn("munmap()");
		m->data=NULL;
		return NULL;
	}
	if(length>m->length)
	{
		lseek(m->fd,length-1,SEEK_SET);
		int c=0;
		write(m->fd,&c,1);
	}
	m->data=mmap(m->data,length,PROT_READ|PROT_WRITE,MAP_SHARED,m->fd,0L);
	if(m->data==MAP_FAILED)
	{
		warn("nmap()");
		m->data=NULL;
		return NULL;
	}
	m->position=m->data;
	m->length=length;
	return m->data;
}

mapped_t* mapped_load(const char* filename)
{
	mapped_t* m=MALLOC(sizeof(mapped_t));
	m->fd=open(filename,O_RDWR);
	if(m->fd==-1)
	{
		warn("open(%s)",filename);
		FREE(m);
		return NULL;
	}
	m->length=lseek(m->fd,0,SEEK_END);
	if(m->length==0)
	{
		int c=0;
		write(m->fd,&c,1);
		m->length=1;
	}
	lseek(m->fd,0,SEEK_SET);
	m->data=mmap(NULL,m->length,PROT_READ|PROT_WRITE,MAP_SHARED,m->fd,0L);
	m->position=m->data;
	return m;
}

mapped_t* mapped_load_readonly(const char* filename)
{
	mapped_t* m=MALLOC(sizeof(mapped_t));
	m->fd=open(filename,O_RDONLY);
	if(m->fd==-1)
	{
		warn("open(%s)",filename);
		FREE(m);
		return NULL;
	}
	m->length=lseek(m->fd,0,SEEK_END);
	lseek(m->fd,0,SEEK_SET);
	m->data=mmap(NULL,m->length,PROT_READ,MAP_SHARED,m->fd,0L);
	m->position=m->data;
	return m;
}

char* mapped_nextline(mapped_t* m)
{
	while((char*)m->position-(char*)m->data<m->length && *((char*)m->position)!='\n')m->position++;
	m->position++;
	if((char*)m->position-(char*)m->data>=m->length)return NULL;
	return m->position;
}

string_t* mapped_readline(mapped_t* m)
{
	int length=0;
	if((char*)m->position-(char*)m->data>=m->length)return NULL;
	while((char*)m->position-(char*)m->data<m->length-length && *((char*)m->position+length)!='\n')length++;
	string_t* output=string_new_empty();
	string_resize(output,length+1);
	memcpy(output->data,m->position,length);
	output->data[length]='\0';
	output->length=length;
	m->position+=length+1;
	return output;
}

char* mapped_gets(mapped_t* m, char* buffer, int size)
{
	int length=0;
	if((char*)m->position-(char*)m->data>=m->length)return NULL;
	while(length<size && (char*)m->position-(char*)m->data<m->length-length && *((char*)m->position+length)!='\n')length++;
	memcpy(buffer,m->position,length);
	buffer[length]='\0';
	m->position+=length+1;
	return buffer;
}

void mapped_free(mapped_t* m)
{
	munmap(m->data,m->length);
	close(m->fd);
	FREE(m);
}
