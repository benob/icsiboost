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

#include "vector.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

size_t vector_memory_size(vector_t* input)
{
	size_t size=sizeof(vector_t);
	size+=input->size*sizeof(void*);
	return size;
}

void _vector_resize(vector_t* v,size_t newSize)
{
	if(newSize==v->size)return;
	if(newSize<1)newSize=1;
	v->data.as_void=REALLOC(v->data.as_void,v->element_size*newSize);
	/*void* newData=MALLOC(sizeof(void*)*newSize);
	if(v->data.as_void!=NULL)
	{
		memcpy(newData,v->data.as_void,v->length*sizeof(void*));
		FREE(v->data.as_void);
	}
	v->data.as_void=newData;*/
	if(v->data.as_void==NULL)warn("_vector_resize(%zd), reallocating vector from %zd", newSize, v->size);
	v->size=newSize;
}

void vector_optimize(vector_t* v)
{
	_vector_resize(v,v->length);
}

void vector_free(vector_t* v)
{
	FREE(v->data.as_void);
	FREE(v);
}

void vector_push(vector_t* v,void* data)
{
	if(v->length>=v->size)
	{
		_vector_resize(v,v->size+v->size/2+1);
	}
	v->data.as_void[v->length]=data;
	v->length++;
}

void* vector_pop(vector_t* v)
{
	if(v->length==0)return NULL;
	void* value=v->data.as_void[v->length-1];
	v->length--;
	if(v->size>2 && v->length<v->size/2)_vector_resize(v,v->length+v->length/2);
	return value;
}

void* vector_remove_element(vector_t* v, size_t index)
{
	if(index<0 || index>=v->length)return NULL;
	void* element=v->data.as_void[index];
	//fprintf(stderr,"%d %d %p %p %d\n",index,v->length,&v->data.as_void[index],&v->data.as_void[index+1],sizeof(void*)*(v->length-index-1));
	if(index!=v->length-1)memmove(&v->data.as_void[index],&v->data.as_void[index+1],sizeof(void*)*(v->length-index-1));
	v->length--;
	if(v->size>2 && v->length<v->size/2)_vector_resize(v,v->length+v->length/2);
	return element;
}

void* vector_shift(vector_t* v)
{
	return vector_remove_element(v,0);
}

void vector_unshift(vector_t* v, void* value)
{
	if(v->length>0)
	{
		vector_push(v,v->data.as_void[v->length-1]);
		memmove(&v->data.as_void[1],&v->data.as_void[0],sizeof(void*)*(v->length-2));
		v->data.as_void[0]=value;
	}
	else
	{
		vector_push(v,value);
	}
}

void vector_append(vector_t* v,vector_t* u)
{
	if(v->length+u->length>=v->size)_vector_resize(v,((v->length+u->length)*3)/2+1);
	memmove(&v->data.as_void[v->length],u->data.as_void,sizeof(void*)*(u->length));
	v->length+=u->length;
}

void vector_prepend(vector_t* v,vector_t* u)
{
	if(v->length+u->length>=v->size)_vector_resize(v,((v->length+u->length)*3)/2+1);
	memmove(&v->data.as_void[u->length],v->data.as_void,sizeof(void*)*(v->length));
	memmove(v->data.as_void,u->data.as_void,sizeof(void*)*(u->length));
	v->length+=u->length;
}

void vector_insert_element(vector_t* v, size_t index, void* value)
{
	if(index<0) vector_unshift(v, value);
	else if(index>=v->length) vector_push(v, value);
	else {
		if(v->length>=v->size)
		{
			_vector_resize(v,v->size+v->size/2+1);
		}
		memmove(&v->data.as_void[index+1],&v->data.as_void[index],sizeof(void*)*(v->length-index-1));
		v->length++;
		v->data.as_void[index]=value;
	}
}

void vector_insert(vector_t* v, size_t index, vector_t* peer)
{
	if(index<0) vector_prepend(v, peer);
	else if(index>=v->length) vector_append(v, peer);
	else {
		if(v->length+peer->length>=v->size)
		{
			_vector_resize(v,v->size+peer->size+v->size/2+1);
		}
		memmove(&v->data.as_void[index+peer->length],&v->data.as_void[index],sizeof(void*)*(v->length-index-1));
		v->length+=peer->length;
		memcpy(&v->data.as_void[index],peer->data.as_void,peer->length*sizeof(void*));
	}
}

vector_t* vector_fusion(vector_t* v, vector_t* u)
{
	vector_append(v,u);
	vector_free(u);
	return v;
}

void vector_reverse(vector_t* v)
{
	size_t i=0;
	size_t j=v->length-1;
	for(;i<v->length/2;i++)
	{
		void* tmp=v->data.as_void[i];
		v->data.as_void[i]=v->data.as_void[j];
		v->data.as_void[j]=tmp;
		j--;
	}
}

void vector_remove(vector_t* v, size_t from, size_t to)
{
	if(from<0)from=0;
	if(to>v->length)to=v->length;
	if(to<=from)return;
	if(to!=v->length)
	{
		memmove(&v->data.as_void[from],&v->data.as_void[to],sizeof(void*)*(to-from));
	}
	v->length-=to-from;
	if(v->length<v->size/2)_vector_resize(v,v->length+v->length/2);
}

vector_t* vector_new(size_t initialSize)
{
	vector_t* v=MALLOC(sizeof(vector_t));
	if(v==NULL)return NULL;
	v->data.as_void=NULL;//MALLOC(sizeof(void*)*initialSize);
	v->size=0;
	v->element_size=sizeof(void*);
	v->length=0;
	_vector_resize(v,initialSize);
	if(v->data.as_void==NULL)
	{
		FREE(v);
		return NULL;
	}
	return v;
}

vector_t* vector_new_type(size_t initialSize, size_t element_size)
{
	vector_t* v=MALLOC(sizeof(vector_t));
	if(v==NULL)return NULL;
	v->data.as_void=NULL;//MALLOC(sizeof(void*)*initialSize);
	v->size=0;
	v->element_size=sizeof(void*);
	v->length=0;
	_vector_resize(v,initialSize);
	if(v->data.as_void==NULL)
	{
		FREE(v);
		return NULL;
	}
	return v;
}

vector_t* vector_subpart(vector_t* v, size_t from, size_t to)
{
	if(from<0)from=0;
	if(to>v->length)to=v->length;
	vector_t* output=vector_new(to-from);
	memcpy(output->data.as_void,&v->data.as_void[from],(to-from)*sizeof(void*));
	output->length=to-from;
	return output;
}

void* vector_get(vector_t* v, size_t index)
{
	if(index>=v->length) warn("vector_get(%zd), out-of-bounds, index>=%zd", index, v->length);
	return v->data.as_void[index];
}

float vector_get_float(vector_t* v, size_t index)
{
	if(index>=v->length) warn("vector_get_float(%zd), out-of-bounds, index>=%zd", index, v->length);
	return v->data.as_float[index];
}

int32_t vector_get_int32(vector_t* v, size_t index)
{
	if(index>=v->length) warn("vector_get_int32(%zd), out-of-bounds, index>=%zd", index, v->length);
	return v->data.as_int32[index];
}

void vector_set(vector_t* v, size_t index, void* value)
{
	if(index>=v->length) warn("vector_set(%zd, %p), out-of-bounds, index>=%zd", index, value, v->length);
	v->data.as_void[index]=value;
}

void vector_set_int32(vector_t* v, size_t index, int32_t value)
{
	if(index>=v->length) warn("vector_set_int32(%zd, %d), out-of-bounds, index>=%zd", index, value, v->length);
	v->data.as_int32[index]=value;
}

void vector_set_float(vector_t* v, size_t index, float value)
{
	if(index>=v->length) warn("vector_set_float(%zd, %f), out-of-bounds, index>=%zd", index, value, v->length);
	v->data.as_float[index]=value;
}

vector_t* vector_copy(vector_t* input)
{
	vector_t* output=vector_new_type(input->length,input->element_size);
	memcpy(output->data.as_void,input->data.as_void,input->length*sizeof(void*));
	output->length=input->length;
	return output;
}

vector_t* vector_duplicate_content(vector_t* input, size_t value_size)
{
	vector_t* output=vector_new(input->length);
	size_t i;
	for(i=0;i<input->length;i++)
	{
		output->data.as_void[i]=(void*)MALLOC(value_size);
		memcpy(output->data.as_void[i],input->data.as_void[i],value_size);
	}
	output->length=input->length;
	return output;
}

void vector_remove_duplicates(vector_t* v)
{
	size_t i,j=0;
	for(i=0;i<v->length && j<v->length;i++)
	{
		v->data.as_void[i]=v->data.as_void[j];
		j++;
		while(j<v->length && v->data.as_void[i]==v->data.as_void[j])j++;
	}
	v->length=i;
}

size_t vector_search(vector_t* v, void* value)
{
	size_t i;
	for(i=0;i<v->length;i++)
	{
		if(v->data.as_void[i]==value)return i;
	}
	return -1;
}

size_t vector_search_sorted(vector_t* v, void* value, int (*comparator)(const void*,const void*))
{
	void** found=bsearch(&value,v->data.as_void,v->length,sizeof(void*),comparator);
	if(found!=NULL)return found-v->data.as_void;
	return -1;
}

void vector_sort(vector_t* v,int (*comparator)(const void*,const void*))
{
	qsort(v->data.as_void,v->length,sizeof(void*),comparator);
}

void vector_apply(vector_t* v, void (*callback)(void* data, void* metadata),void* metadata)
{
	size_t i;
	for(i=0;i<v->length;i++)callback(v->data.as_void[i],metadata);
}

void vector_freedata(void* data,void* metadata)
{
	FREE(data);
}
