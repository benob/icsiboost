/* Copyright (C) (2007) (Benoit Favre) <favre@icsi.berkeley.edu>

This program is free software; you can redistribute it and/or 
modify it under the terms of the GNU General Public License 
as published by the Free Software Foundation; either 
version 2 of the License, or (at your option) any later 
version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA. */

#include "hashtable.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define MIN(a,b) ((a)>(b)?(b):(a))
#define HASHTABLE_VERSION 2
#define HASHTABLE_MAGIC 0xabcdef00+HASHTABLE_VERSION

unsigned int _hashtable_function(void* key,int key_length)
{
	unsigned int hash = 0;
	int i;
	for(i=0;i<key_length;i++)
	{
		hash += *(char*)(key+i);
		hash += (hash << 10);
		hash ^= (hash >> 6);
	}
	hash += (hash << 3);
	hash ^= (hash >> 11);
	hash += (hash << 15);
	return hash;
}

int _hashtable_key_equals(void* key1, int key1_length, void* key2, int key2_length)
{
	int i;
	if(key1_length!=key2_length)return 0;
	for(i=0;i<key1_length;i++)if(*(char*)(key1+i)!=*(char*)(key2+i))return 0;
	return 1;
}

hashtable_t* hashtable_new(int size)
{
	hashtable_t* h=MALLOC(sizeof(hashtable_t));
	if(h==NULL)return NULL;
	h->size=size;
	h->buckets=MALLOC(sizeof(vector_t*)*size);
	if(h->buckets==NULL)
	{
		FREE(h);
		return NULL;
	}
	memset(h->buckets,0,sizeof(vector_t*)*size);
	return h;
}

void hashtable_set(hashtable_t* h,void* key,int key_length,void* value)
{
	int i;
	int bucket=_hashtable_function(key,key_length)%h->size;
	if(h->buckets[bucket]==NULL)h->buckets[bucket]=vector_new(16);
	vector_t* v=h->buckets[bucket];
	for(i=0;i<v->length;i++)
	{
		hashelement_t* element=(hashelement_t*)vector_get(v,i);
		if(_hashtable_key_equals(element->key,element->key_length,key,key_length))
		{
			element->value=value;
			return;
		}
	}
	hashelement_t* element=MALLOC(sizeof(hashelement_t));
	element->key=MALLOC(key_length);
	memcpy(element->key,key,key_length);
	element->key_length=key_length;
	element->value=value;
	vector_push(v,element);
}

void* hashtable_remove(hashtable_t* h,void* key,int key_length)
{
	int i;
	int bucket=_hashtable_function(key,key_length)%h->size;
	if(h->buckets[bucket]==NULL)return NULL;
	vector_t* v=h->buckets[bucket];
	for(i=0;i<v->length;i++)
	{
		hashelement_t* element=(hashelement_t*)vector_get(v,i);
		if(_hashtable_key_equals(element->key,element->key_length,key,key_length))
		{
			//cheaper removal: swap elements instead of vector_remove(v,i);
			vector_set(v,i,vector_get(v,v->length-1));
			v->length--;
			if(v->length<v->size/2)_vector_resize(v,v->length+v->length/2);
			void* value=element->value;
			FREE(element->key);
			FREE(element);
			return value;
		}
	}
	return NULL;
}

void* hashtable_get(hashtable_t* h,void* key,int key_length)
{
	int i;
	int bucket=_hashtable_function(key,key_length)%h->size;
	if(h->buckets[bucket]==NULL)return NULL;
	vector_t* v=h->buckets[bucket];
	for(i=0;i<v->length;i++)
	{
		hashelement_t* element=(hashelement_t*)vector_get(v,i);
		if(_hashtable_key_equals(element->key,element->key_length,key,key_length))
		{
			return element->value;
		}
	}
	return NULL;
}

void* hashtable_get_or_default(hashtable_t* h,void* key,int key_length,void* defaultValue)
{
	int i;
	int bucket=_hashtable_function(key,key_length)%h->size;
	if(h->buckets[bucket]==NULL)return defaultValue;
	vector_t* v=h->buckets[bucket];
	for(i=0;i<v->length;i++)
	{
		hashelement_t* element=(hashelement_t*)vector_get(v,i);
		if(_hashtable_key_equals(element->key,element->key_length,key,key_length))
		{
			return element->value;
		}
	}
	return defaultValue;
}

off_t hashtable_get_from_file(FILE* file,void* key,int key_length)
{
	int j;
	int size;
	off_t begining=ftello(file);
	int magic=-1;
	fread(&magic,sizeof(int),1,file);
	if(magic!=HASHTABLE_MAGIC)
	{
		warn("bad magic %d!=%d",magic,HASHTABLE_MAGIC);
		return (off_t)-1;
	}
	fread(&size,sizeof(int),1,file);
	int bucket=_hashtable_function(key,key_length)%size;
	off_t bucketLocation=-1;
	int bucketLength=-1,bucketSize=-1;
	fseeko(file,(off_t)(bucket*(sizeof(int)*2+sizeof(off_t))),SEEK_CUR);
	fread(&bucketLocation,sizeof(off_t),1,file);
	fread(&bucketLength,sizeof(int),1,file);
	fread(&bucketSize,sizeof(int),1,file);
	//warn("bucket=%d location=%lld length=%d size=%d",bucket,bucketLocation,bucketLength,bucketSize);
	if(bucketLength!=0)
	{
		fseeko(file,bucketLocation,SEEK_SET);
		char buffer[bucketSize];
		fread(buffer,bucketSize,1,file);
		char* ptr=buffer;
		for(j=0;j<bucketLength;j++)
		{
			int read_key_length=*(int*)ptr;
			ptr+=sizeof(int);
			//warn("%p %d %p %d\n",key,key_length,ptr,read_key_length);
			if(_hashtable_key_equals(key,key_length,ptr,read_key_length))
			{
				ptr+=read_key_length;
				off_t location=*(off_t*)ptr;
				fseeko(file,begining,SEEK_SET);
				return location;
			}
			ptr+=read_key_length+sizeof(off_t);
		}
	}
	fseeko(file,begining,SEEK_SET);
	//warn("not found");
	return -1;
}

off_t hashtable_get_from_mapped(mapped_t* mapped,void* key,int key_length)
{
	int j;
	int pointer=0;
	int magic=*(int*)(mapped->data);
	if(magic!=HASHTABLE_MAGIC)
	{
		warn("bad magic %x!=%x",magic,HASHTABLE_MAGIC);
		return (off_t)-1;
	}
	pointer+=sizeof(int);
	int size=*(int*)(mapped->data+pointer);
	pointer+=sizeof(int);
	int bucket=_hashtable_function(key,key_length)%size;
	pointer+=bucket*(sizeof(int)*2+sizeof(off_t));
	off_t bucketLocation=*(off_t*)(mapped->data+pointer);
	pointer+=sizeof(off_t);
	int bucketLength=*(int*)(mapped->data+pointer);
	pointer+=sizeof(int);
	//int bucketSize=*(int*)(mapped->data+pointer);
	pointer+=sizeof(int);
	//fprintf(stderr,"magic=%x size=%d bucket=%d bucketLocation=%lld bucketLength=%d\n",magic,size,bucket,bucketLocation,bucketLength);
	if(bucketLength!=0)
	{
		pointer=bucketLocation;
		char* buffer=(char*)(mapped->data+pointer);
		char* ptr=buffer;
		for(j=0;j<bucketLength;j++)
		{
			//fprintf(stderr,"j=%d ptr=%d\n",j,ptr-buffer);
			int read_key_length=*(int*)ptr;
			ptr+=sizeof(int);
			if(_hashtable_key_equals(key,key_length,ptr,read_key_length))
			{
				ptr+=read_key_length;
				off_t location=*(off_t*)ptr;
				return location;
			}
			ptr+=read_key_length+sizeof(off_t);
		}
	}
	return (off_t)-1;
}

void _hashtable_freeelement(void* data, void* metadata)
{
	hashelement_t* element=(hashelement_t*)data;
	FREE(element->key);
	FREE(element);
}

void hashtable_free(hashtable_t* h)
{
	int i;
	for(i=0;i<h->size;i++)
		if(h->buckets[i]!=NULL)
		{
			vector_apply(h->buckets[i],_hashtable_freeelement,NULL);
			vector_free(h->buckets[i]);
		}
	FREE(h->buckets);
	FREE(h);
}

void hashtable_optimize(hashtable_t* h)//, int (*compare)(const void* a,const void* b))
{
	int i;
	for(i=0;i<h->size;i++)
	{
		if(h->buckets[i]!=NULL)
		{
			_vector_resize(h->buckets[i],h->buckets[i]->length);
			//if(compare!=NULL)qsort(h->buckets[i]->data,h->buckets[i]->length,sizeof(void*),compare);
		}
	}
}

int hashtable_memory_size(hashtable_t* h)
{
	int size=sizeof(hashtable_t);
	int i;
	size+=h->size*sizeof(vector_t*);
	for(i=0;i<h->size;i++)
	{
		if(h->buckets[i]!=NULL)
			size+=h->buckets[i]->size*(sizeof(void*)+sizeof(hashelement_t))+sizeof(vector_t);
	}
	return size;
}

void hashtable_stats(hashtable_t* h,FILE* stream)
{
	int elements=0;
	int capacity=0;
	int size=0;
	int memory=0;
	int i,j;
	for(i=0;i<h->size;i++)
	{
		if(h->buckets[i]!=NULL)
		{
			capacity+=h->buckets[i]->size;
			memory+=h->buckets[i]->size*(sizeof(void*)+sizeof(hashelement_t))+sizeof(vector_t);
			for(j=0;j<h->buckets[i]->length;j++)
			{
				hashelement_t* element=(hashelement_t*)vector_get(h->buckets[i],j);
				memory+=element->key_length;
			}
			size++;
			elements+=h->buckets[i]->length;
		}
	}
	memory+=h->size*sizeof(vector_t*);
	memory+=sizeof(hashtable_t);
	fprintf(stream,"hashtable_stat(%p) memory=%dk cost=%.2fbpe elements=%d capacity=%d size=%d/%zd average=%.2f load=%.2f\n",
			h,memory/1024,(double)memory/elements,elements,capacity,size,h->size,(double)elements/size,(double)elements/capacity);
}

int hashtable_save(hashtable_t* h,FILE* file, off_t (*saveValue)(hashelement_t* element,void* metadata), void* metadata)
{
	int i,j;
	off_t bucketLocation[h->size];
	off_t begining=ftello(file);
	fseeko(file,h->size*(sizeof(int)*2+sizeof(off_t))+sizeof(int)*2,SEEK_CUR);
	for(i=0;i<h->size;i++)
	{
		bucketLocation[i]=ftello(file);
		if(h->buckets[i]!=NULL)
		{
			vector_t* v=h->buckets[i];
			for(j=0;j<v->length;j++)
			{
				hashelement_t* element=vector_get(v,j);
				fwrite(&element->key_length,sizeof(int),1,file);
				fwrite(element->key,element->key_length,1,file);
				if(saveValue!=NULL)
				{
					off_t location=saveValue(element, metadata);
					fwrite(&location,sizeof(off_t),1,file);
				}
				else
				{
					fprintf(stderr,">value %p\n",element->value);
					fwrite(&element->value,sizeof(void*),1,file);
				}
			}
		}
	}
	off_t end=ftello(file);
	fseeko(file,begining,SEEK_SET);
	int magic=HASHTABLE_MAGIC;
	fwrite(&magic,sizeof(int),1,file);
	fwrite(&(h->size),sizeof(int),1,file);
	// magic, size, size*[location,length,size]
	int zero=0;
	for(i=0;i<h->size;i++)
	{
		fwrite(&bucketLocation[i],sizeof(off_t),1,file);
		if(h->buckets[i]!=NULL)
		{
			fwrite(&h->buckets[i]->length,sizeof(int),1,file);
			int diskSize=0;
			if(i<h->size-1)diskSize=bucketLocation[i+1]-bucketLocation[i];
			else diskSize=end-bucketLocation[i];
			fwrite(&diskSize,sizeof(int),1,file);
			//warn("%d %lld %d %d",i,bucketLocation[i],h->buckets[i]->length,diskSize);
		}
		else
		{
			//warn("%d %lld %d %d",i,bucketLocation[i],0,0)
			fwrite(&zero,sizeof(int),1,file);
			fwrite(&zero,sizeof(int),1,file);
		}
	}
	fseeko(file,end,SEEK_SET);
	return 0;
}

hashtable_t* hashtable_load(FILE* file, void* (*loadValue)(void* key,int key_length,off_t location,void* metadata),void* metadata)
{
	int i,j;
	int size;
	int magic=-1;
	fread(&magic,sizeof(int),1,file);
	if(magic!=HASHTABLE_MAGIC)
	{
		warn("bad magic %d!=%d",magic,HASHTABLE_MAGIC);
		return NULL;
	}
	fread(&size,sizeof(int),1,file);
	hashtable_t* h=hashtable_new(size);
	off_t bucketLocation[size];
	int bucketLength[size];
	int bucketSize[size];
	for(i=0;i<h->size;i++)
	{
		fread(&bucketLocation[i],sizeof(off_t),1,file);
		fread(&bucketLength[i],sizeof(int),1,file);
		fread(&bucketSize[i],sizeof(int),1,file);
	}
	for(i=0;i<h->size;i++)
	{
		//warn("%d %lld %d %d",i,bucketLocation[i],bucketLength[i],bucketSize[i]);
		if(bucketLength[i]!=0)
		{
			h->buckets[i]=vector_new(bucketLength[i]);
			fseeko(file,bucketLocation[i],SEEK_SET);
			char buffer[bucketSize[i]];
			fread(buffer,bucketSize[i],1,file);
			char* ptr=buffer;
			for(j=0;j<bucketLength[i];j++)
			{
				hashelement_t* element=MALLOC(sizeof(hashelement_t));
				element->key_length=*(int*)ptr;
				ptr+=sizeof(int);
				element->key=MALLOC(element->key_length);
				memcpy(element->key,ptr,element->key_length);
				ptr+=element->key_length;
				if(loadValue!=NULL)
				{
					element->value=loadValue(element->key,element->key_length,*(off_t*)ptr,metadata);
					ptr+=sizeof(off_t);
				}
				else
				{
					element->value=*(void**)ptr;
					ptr+=sizeof(void*);
				}
				vector_push(h->buckets[i],element);
			}
		}
		else
		{
			h->buckets[i]=NULL;
		}
	}
	return h;
}

void hashtable_apply(hashtable_t* h, void (*callback)(hashelement_t* element, void* metadata),void* metadata)
{
	int i,j;
	for(i=0;i<h->size;i++)
	{
		if(h->buckets[i]!=NULL)
		{
			for(j=0;j<h->buckets[i]->length;j++)
			{
				hashelement_t* element=(hashelement_t*)vector_get(h->buckets[i],j);
				callback(element,metadata);
			}
		}
	}
}

vector_t* hashtable_elements(hashtable_t* h)
{
	int i;
	vector_t* output=vector_new(16);
	for(i=0;i<h->size;i++)
	{
		if(h->buckets[i]!=NULL)
		{
			vector_append(output,h->buckets[i]);
		}
	}
	return output;
}

vector_t* hashtable_keys(hashtable_t* h)
{
	int i;
	vector_t* output=vector_new(16);
	for(i=0;i<h->size;i++)
	{
		if(h->buckets[i]!=NULL)
		{
			int j;
			for(j=0;j<h->buckets[i]->length;j++)
			{
				hashelement_t* element=(hashelement_t*)vector_get(h->buckets[i],j);
				vector_push(output, element->key);
			}
		}
	}
	return output;
}

vector_t* hashtable_values(hashtable_t* h)
{
	int i;
	vector_t* output=vector_new(16);
	for(i=0;i<h->size;i++)
	{
		if(h->buckets[i]!=NULL)
		{
			int j;
			for(j=0;j<h->buckets[i]->length;j++)
			{
				hashelement_t* element=(hashelement_t*)vector_get(h->buckets[i],j);
				vector_push(output, element->value);
			}
		}
	}
	return output;
}

hashelement_t* hashtable_first_element(hashtable_t* h)
{
	int i;
	h->current_bucket=-1;
	h->current_bucket_element=-1;
	for(i=0;i<h->size;i++)
	{
		if(h->buckets[i]!=NULL && h->buckets[i]->length>0)
		{
			h->current_bucket=i;
			h->current_bucket_element=0;
			hashelement_t* element=(hashelement_t*)vector_get(h->buckets[i],h->current_bucket_element);
			return element;
		}
	}
	return NULL;
}

hashelement_t* hashtable_next_element(hashtable_t* h)
{
	int i;
	if(h->current_bucket==-1 || h->current_bucket_element==-1)return NULL;
	h->current_bucket_element++;
	for(i=h->current_bucket;i<h->size;i++)
	{
		if(h->buckets[i]!=NULL && h->buckets[i]->length>0)
		{
			if(h->current_bucket_element>=h->buckets[i]->length)
			{
				h->current_bucket_element=0;
				continue;
			}
			h->current_bucket=i;
			hashelement_t* element=(hashelement_t*)vector_get(h->buckets[i],h->current_bucket_element);
			return element;
		}
	}
	h->current_bucket=-1;
	h->current_bucket_element=-1;
	return NULL;
}

void* hashtable_first_key(hashtable_t* h)
{
	hashelement_t* element=hashtable_first_element(h);
	if(element==NULL)return NULL;
	return element->key;
}

void* hashtable_next_key(hashtable_t* h)
{
	hashelement_t* element=hashtable_next_element(h);
	if(element==NULL)return NULL;
	return element->key;
}

void* hashtable_first_value(hashtable_t* h)
{
	hashelement_t* element=hashtable_first_element(h);
	if(element==NULL)return NULL;
	return element->value;
}

void* hashtable_next_value(hashtable_t* h)
{
	hashelement_t* element=hashtable_next_element(h);
	if(element==NULL)return NULL;
	return element->value;
}

void hashtable_freevalue(hashelement_t* element, void* metadata)
{
	FREE(element->value);
}

void _hashtable_replicate(hashelement_t* element, void* metadata)
{
	hashtable_t* h=(hashtable_t*)metadata;
	hashtable_set(h,element->key,element->key_length,element->value);
}

hashtable_t* hashtable_resize(hashtable_t* h,int newSize)
{
	if(newSize==h->size)return h;
	hashtable_t* output=hashtable_new(newSize);
	hashtable_apply(h,_hashtable_replicate,output);
	hashtable_free(h);
	return output;
}

