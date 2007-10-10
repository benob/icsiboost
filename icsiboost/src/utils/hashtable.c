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
#define HASHTABLE_VERSION 3
#define HASHTABLE_MAGIC 0xabcdef00+HASHTABLE_VERSION

#ifdef HASHTABLE_CENTRALIZE_KEYS
hashtable_t* _hashtable_key_repository=NULL;
#endif

uint32_t _hashtable_function(const void* key,size_t key_length)
{
	uint32_t hash = 0;
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

int _hashtable_key_equals(const void* key1, size_t key1_length, const void* key2, size_t key2_length)
{
	if(key1_length!=key2_length)return 0;
	return !memcmp(key1,key2,key1_length);
}

hashtable_t* hashtable_new_size_collisions_factor(size_t initial_capacity, size_t max_average_collisions, double resize_factor)
{
	hashtable_t* output=MALLOC(sizeof(hashtable_t));
	output->buckets=MALLOC(sizeof(hashelement_t*)*initial_capacity);
	memset(output->buckets,0,sizeof(hashelement_t*)*initial_capacity);
	output->length=0;
	output->size=initial_capacity;
	output->current_element=NULL;
#ifdef HASHTABLE_INCREASE_SIZE
	output->max_average_collisions=max_average_collisions;
	output->resize_factor=resize_factor;
#endif
#ifdef HASHTABLE_GATHER_STATS
	output->num_accesses=0;
	output->num_hits_per_access=0;
#endif
	return output;
}

hashtable_t* string_array_to_hashtable(array_t* input)
{
	hashtable_t* output=hashtable_new();
	int i;
	if(input->length % 2 != 0)warn("hashtable_new_from_string_array(%p), odd number of element in array (%zd), last element ignored", input, input->length);
	for(i=0; i<input->length; i+=2)
	{
		string_t* key=array_get(input, i);
		string_t* value=array_get(input, i+1);
		hashtable_set(output, key->data, key->length, string_copy(value));
	}
	return output;
}

array_t* string_array_from_hashtable(hashtable_t* input)
{
	array_t* output=array_new();
	hashelement_t* element=hashtable_first_element(input);
	while(element!=NULL)
	{
		array_push(output, string_new_from_to(element->key, 0, element->key_length));
		array_push(output, string_copy(element->value));
		element=hashtable_next_element(input);
	}
	return output;
}

void hashtable_set(hashtable_t* input, const void* key, size_t key_length, void* value)
{
	//fprintf(stderr,"store(%s,%p)\n",key,value);
	uint32_t hashcode=_hashtable_function(key, key_length);
	uint32_t bucket=hashcode%input->size;
	hashelement_t* element=input->buckets[bucket];
#ifdef HASHTABLE_REORDER_ON_ACCESS
	hashelement_t* previous_element=NULL;
#endif
#ifdef HASHTABLE_GATHER_STATS
	unsigned int num_hits=1;
#endif
	while(element!=NULL)
	{
		if(element->key_length==key_length && memcmp(element->key,key,key_length)==0)
		{
			element->value=value;
#ifdef HASHTABLE_GATHER_STATS
			input->num_hits_per_access+=num_hits;
			input->num_accesses++;
#endif
#ifdef HASHTABLE_REORDER_ON_ACCESS
			if(previous_element!=NULL)
			{
				previous_element->next_in_bucket=element->next_in_bucket;
				element->next_in_bucket=input->buckets[bucket];
				input->buckets[bucket]=element;
			}
#endif
			return;
		}
#ifdef HASHTABLE_GATHER_STATS
		num_hits++;
#endif
#ifdef HASHTABLE_REORDER_ON_ACCESS
		previous_element=element;
#endif
		element=element->next_in_bucket;
	}
#ifdef HASHTABLE_GATHER_STATS
	input->num_hits_per_access+=num_hits;
	input->num_accesses++;
#endif
	element=MALLOC(sizeof(hashelement_t));
	element->hashcode=hashcode;
#ifdef HASHTABLE_CENTRALIZE_KEYS
	if(input==_hashtable_key_repository)
	{
		element->key=key;
	}
	else
	{
		if(_hashtable_key_repository==NULL)
			_hashtable_key_repository=hashtable_new();
		char* repository_key=hashtable_fetch(_hashtable_key_repository, key, key_length);
		if(repository_key==NULL)
		{
			repository_key=MALLOC(key_length);
			memcpy(repository_key,key,key_length);
			hashtable_store(_hashtable_key_repository, repository_key, key_length, repository_key);
		}
		element->key=repository_key;
	}
#else
	element->key=MALLOC(key_length);
	memcpy(element->key,key,key_length);
#endif
	element->key_length=key_length;
	element->value=value;
	element->next_in_bucket=input->buckets[bucket];
	input->buckets[bucket]=element;
	input->length++;
#ifdef HASHTABLE_INCREASE_SIZE
	if(input->length/input->size>input->max_average_collisions)hashtable_resize(input, (size_t)(input->size*(input->resize_factor)+1));//+input->size);
#endif
}

void* hashtable_remove(hashtable_t* input, const void* key, size_t key_length)
{
	uint32_t hashcode=_hashtable_function(key, key_length);
	uint32_t bucket=hashcode%input->size;
	hashelement_t* element=input->buckets[bucket];
	hashelement_t* previous_element=NULL;
#ifdef HASHTABLE_GATHER_STATS
	unsigned int num_hits=0;
#endif
	while(element!=NULL)
	{
		if(element->key_length==key_length && memcmp(element->key,key,key_length)==0)
		{
			void* value=element->value;
			if(previous_element==NULL)
				input->buckets[bucket]=element->next_in_bucket;
			else
				previous_element->next_in_bucket=element->next_in_bucket;
			FREE(element->key);
			FREE(element);
			input->length--;
#ifdef HASHTABLE_GATHER_STATS
			input->num_accesses++;
			input->num_hits_per_access+=num_hits;
#endif
			return value;
		}
#ifdef HASHTABLE_GATHER_STATS
		num_hits++;
#endif
		previous_element=element;
		element=element->next_in_bucket;
	}
#ifdef HASHTABLE_GATHER_STATS
	input->num_accesses++;
	input->num_hits_per_access+=num_hits;
#endif
	return NULL;
}

void* hashtable_get(hashtable_t* input, const void* key, size_t key_length)
{
	//fprintf(stderr,"fetch(%s)\n",key);
	uint32_t hashcode=_hashtable_function(key, key_length);
	uint32_t bucket=hashcode%input->size;
	if(input->buckets[bucket]==NULL)return NULL;
	hashelement_t* element=input->buckets[bucket];
#ifdef HASHTABLE_REORDER_ON_ACCESS
	hashelement_t* previous_element=NULL;
#endif
#ifdef HASHTABLE_GATHER_STATS
	unsigned int num_hits=1;
#endif
	while(element!=NULL)
	{
		if(_hashtable_key_equals(element->key,element->key_length,key,key_length))
		{
#ifdef HASHTABLE_GATHER_STATS
			input->num_hits_per_access+=num_hits;
			input->num_accesses++;
#endif
#ifdef HASHTABLE_REORDER_ON_ACCESS
			if(previous_element!=NULL)
			{
				previous_element->next_in_bucket=element->next_in_bucket;
				element->next_in_bucket=input->buckets[bucket];
				input->buckets[bucket]=element;
			}
#endif
			return element->value;
		}
#ifdef HASHTABLE_REORDER_ON_ACCESS
		previous_element=element;
#endif
		element=element->next_in_bucket;
#ifdef HASHTABLE_GATHER_STATS
		num_hits++;
#endif
	}
#ifdef HASHTABLE_GATHER_STATS
	input->num_hits_per_access+=num_hits;
	input->num_accesses++;
#endif
	return NULL;
}

void* hashtable_get_or_default(hashtable_t* h,const void* key,size_t key_length,void* defaultValue)
{
	void* value=hashtable_get(h, key, key_length);
	if(value==NULL)return defaultValue;
	return value;
}

off_t hashtable_get_from_file(FILE* file,const void* key,size_t key_length)
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
	uint32_t bucket=_hashtable_function(key,key_length)%size;
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
			size_t read_key_length=*(size_t*)ptr;
			ptr+=sizeof(size_t);
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

off_t hashtable_get_from_mapped(mapped_t* mapped,const void* key,size_t key_length)
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
	uint32_t bucket=_hashtable_function(key,key_length)%size;
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
			size_t read_key_length=*(size_t*)ptr;
			ptr+=sizeof(size_t);
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
	hashelement_t* element=*(hashelement_t**)data;
	FREE(element->key);
	FREE(element);
}

void hashtable_free(hashtable_t* input)
{
	int i;
	for(i=0;i<input->size;i++)
	{
		hashelement_t* element=input->buckets[i];
		while(element)
		{
			hashelement_t* tmp=element->next_in_bucket;
#ifdef HASHTABLE_CENTRALIZE_KEYS
			if(input==_hashtable_key_repository) FREE(element->key);
#else
			FREE(element->key);
#endif
			FREE(element);
			element=tmp;
		}
	}
	FREE(input->buckets);
	FREE(input);
}


void hashtable_optimize(hashtable_t* h)//, int (*compare)(const void* a,const void* b))
{
	warn("hashtable_optimize(%p) is deprecated", h);
}

size_t hashtable_memory_size(hashtable_t* input)
{
	size_t memory=sizeof(hashtable_t);
	memory+=input->size*sizeof(hashelement_t*);
	memory+=input->size*sizeof(hashelement_t);
#ifdef HASHTABLE_CENTRALIZE_KEYS
	if(input==_hashtable_key_repository)
#endif
	{
		int i;
		for(i=0;i<input->size;i++)
		{
			hashelement_t* element=input->buckets[i];
			while(element)
			{
				memory+=element->key_length;
				element=element->next_in_bucket;
			}
		}
	}
	return memory;
}

void hashtable_stats(hashtable_t* input,FILE* stream)
{
	size_t memory=hashtable_memory_size(input);
	fprintf(stream,"hashtable_stats(%p) size=%zd elements=%zd avg_coll=%.2f"
#ifdef HASHTABLE_GATHER_STATS
		" avg_access=%.2f"
#endif
		" Bpe=%.2f memory=%.2fk\n",
		input,input->size,input->length,input->length/(double)input->size,
#ifdef HASHTABLE_GATHER_STATS
		input->num_hits_per_access/(double)input->num_accesses,
#endif
		memory/(double)input->length,memory/1024.0);
}

int hashtable_save(hashtable_t* h,FILE* file, off_t (*saveValue)(hashelement_t* element,void* metadata), void* metadata)
{
	int i;
	off_t bucketLocation[h->size];
	off_t begining=ftello(file);
	fseeko(file,h->size*(sizeof(int)*2+sizeof(off_t))+sizeof(int)*2,SEEK_CUR);
	size_t *bucketLength=MALLOC(sizeof(size_t)*h->size);
	for(i=0;i<h->size;i++)
	{
		bucketLocation[i]=ftello(file);
		bucketLength[i]=0;
		hashelement_t* element=h->buckets[i];
		while(element!=NULL)
		{
			bucketLength[i]++;
			fwrite(&element->key_length,sizeof(size_t),1,file);
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
			element=element->next_in_bucket;
		}
	}
	off_t end=ftello(file);
	fseeko(file,begining,SEEK_SET);
	int magic=HASHTABLE_MAGIC;
	fwrite(&magic,sizeof(int),1,file);
	fwrite(&(h->size),sizeof(size_t),1,file);
	// magic, size, size*[location,length,size]
	size_t zero=0;
	for(i=0;i<h->size;i++)
	{
		fwrite(&bucketLocation[i],sizeof(off_t),1,file);
		if(h->buckets[i]!=NULL)
		{
			fwrite(&bucketLength[i],sizeof(size_t),1,file);
			size_t diskSize=0;
			if(i<h->size-1)diskSize=bucketLocation[i+1]-bucketLocation[i];
			else diskSize=end-bucketLocation[i];
			fwrite(&diskSize,sizeof(size_t),1,file);
			//warn("%d %lld %d %d",i,bucketLocation[i],h->buckets[i]->length,diskSize);
		}
		else
		{
			//warn("%d %lld %d %d",i,bucketLocation[i],0,0)
			fwrite(&zero,sizeof(size_t),1,file);
			fwrite(&zero,sizeof(size_t),1,file);
		}
	}
	fseeko(file,end,SEEK_SET);
	FREE(bucketLength);
	return 0;
}

hashtable_t* hashtable_load(FILE* file, void* (*loadValue)(const void* key,size_t key_length,off_t location,void* metadata),void* metadata)
{
	int i,j;
	size_t size;
	int magic=-1;
	fread(&magic,sizeof(int),1,file);
	if(magic!=HASHTABLE_MAGIC)
	{
		warn("bad magic %d!=%d",magic,HASHTABLE_MAGIC);
		return NULL;
	}
	fread(&size,sizeof(size_t),1,file);
	hashtable_t* h=hashtable_new_size(size);
	off_t bucketLocation[size];
	size_t bucketLength[size];
	size_t bucketSize[size];
	for(i=0;i<h->size;i++)
	{
		fread(&bucketLocation[i],sizeof(off_t),1,file);
		fread(&bucketLength[i],sizeof(size_t),1,file);
		fread(&bucketSize[i],sizeof(size_t),1,file);
	}
	for(i=0;i<h->size;i++)
	{
		//warn("%d %lld %d %d",i,bucketLocation[i],bucketLength[i],bucketSize[i]);
		if(bucketLength[i]!=0)
		{
			fseeko(file,bucketLocation[i],SEEK_SET);
			char buffer[bucketSize[i]];
			fread(buffer,bucketSize[i],1,file);
			char* ptr=buffer;
			for(j=0;j<bucketLength[i];j++)
			{
				size_t key_length=*(size_t*)ptr;
				ptr+=sizeof(size_t);
				char* key=ptr;
				ptr+=key_length;
				void* value=NULL;
				if(loadValue!=NULL)
				{
					value=loadValue(key,key_length,*(off_t*)ptr,metadata);
					ptr+=sizeof(off_t);
				}
				else
				{
					value=*(void**)ptr;
					ptr+=sizeof(void*);
				}
				hashtable_set(h,key,key_length,value);
			}
		}
	}
	return h;
}

void hashtable_apply(hashtable_t* h, void (*callback)(hashelement_t* element, void* metadata),void* metadata)
{
	int i;
	for(i=0;i<h->size;i++)
	{
		hashelement_t* element=h->buckets[i];
		while(element!=NULL)
		{
			callback(element,metadata);
			element=element->next_in_bucket;
		}
	}
}

vector_t* hashtable_elements(hashtable_t* h)
{
	int i;
	vector_t* output=vector_new(h->length);
	for(i=0;i<h->size;i++)
	{
		hashelement_t* element=h->buckets[i];
		while(element!=NULL)
		{
			vector_push(output,element);
			element=element->next_in_bucket;
		}
	}
	return output;
}

vector_t* hashtable_keys(hashtable_t* h)
{
	int i;
	vector_t* output=vector_new(h->length);
	for(i=0;i<h->size;i++)
	{
		hashelement_t* element=h->buckets[i];
		while(element!=NULL)
		{
			vector_push(output,element->key);
			element=element->next_in_bucket;
		}
	}
	return output;
}

vector_t* hashtable_values(hashtable_t* h)
{
	int i;
	vector_t* output=vector_new(h->length);
	for(i=0;i<h->size;i++)
	{
		hashelement_t* element=h->buckets[i];
		while(element)
		{
			vector_push(output,element->value);
			element=element->next_in_bucket;
		}
	}
	return output;
}

hashelement_t* hashtable_first_element(hashtable_t* h)
{
	int i;
	if(h->length==0)return NULL;
	for(i=0;i<h->size;i++)
	{
		if(h->buckets[i]!=NULL)
		{
			h->current_element=h->buckets[i];
			return h->buckets[i];
		}
	}
	return NULL;
}

hashelement_t* hashtable_next_element(hashtable_t* h)
{
	uint32_t i;
	if(h->current_element->next_in_bucket!=NULL)
	{
		h->current_element=h->current_element->next_in_bucket;
		return h->current_element;
	}
	uint32_t next_bucket=h->current_element->hashcode%h->size+1;
	for(i=next_bucket;i<h->size;i++)
	{
		if(h->buckets[i]!=NULL)
		{
			h->current_element=h->buckets[i];
			return h->buckets[i];
		}
	}
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

void hashtable_resize(hashtable_t* input, size_t new_size)
{
	//hashtable_print_stats(input,stdout);
	hashelement_t** old_buckets=input->buckets;
	input->buckets=MALLOC(sizeof(hashelement_t*)*new_size);
	memset(input->buckets,0,sizeof(hashelement_t*)*new_size);
	int i;
	for(i=0;i<input->size;i++)
	{
		hashelement_t* element=old_buckets[i];
		while(element)
		{
			hashelement_t* tmp=element->next_in_bucket;
			uint32_t bucket=element->hashcode%new_size;
			element->next_in_bucket=input->buckets[bucket];
			input->buckets[bucket]=element;
			element=tmp;
		}
	}
	input->size=new_size;
	FREE(old_buckets);
	//hashtable_print_stats(input,stdout);
}

