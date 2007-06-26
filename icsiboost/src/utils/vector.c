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

int vector_memory_size(vector_t* input)
{
	int size=sizeof(vector_t);
	size+=input->size*sizeof(void*);
	return size;
}

void vector_resize(vector_t* v,int newSize)
{
	if(newSize==v->size)return;
	if(newSize<1)newSize=1;
	v->data=REALLOC(v->data,sizeof(void*)*newSize);
	/*void* newData=MALLOC(sizeof(void*)*newSize);
	if(v->data!=NULL)
	{
		memcpy(newData,v->data,v->length*sizeof(void*));
		FREE(v->data);
	}
	v->data=newData;*/
	if(v->data==NULL)warn("vector_resize(%d), reallocating vector from %d", newSize, v->size);
	v->size=newSize;
}

void vector_optimize(vector_t* v)
{
	vector_resize(v,v->length);
}

void vector_free(vector_t* v)
{
	FREE(v->data);
	FREE(v);
}

void vector_push(vector_t* v,void* data)
{
	if(v->length>=v->size)
	{
		vector_resize(v,v->size+v->size/2+1);
	}
	v->data[v->length]=data;
	v->length++;
}

void* vector_pop(vector_t* v)
{
	if(v->length==0)return NULL;
	void* value=v->data[v->length-1];
	v->length--;
	if(v->size>2 && v->length<v->size/2)vector_resize(v,v->length+v->length/2);
	return value;
}

void* vector_remove_element(vector_t* v, int index)
{
	if(index<0 || index>=v->length)return NULL;
	void* element=v->data[index];
	//fprintf(stderr,"%d %d %p %p %d\n",index,v->length,&v->data[index],&v->data[index+1],sizeof(void*)*(v->length-index-1));
	if(index!=v->length-1)memmove(&v->data[index],&v->data[index+1],sizeof(void*)*(v->length-index-1));
	v->length--;
	if(v->size>2 && v->length<v->size/2)vector_resize(v,v->length+v->length/2);
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
		vector_push(v,v->data[v->length-1]);
		memmove(&v->data[1],&v->data[0],sizeof(void*)*(v->length-2));
		v->data[0]=value;
	}
	else
	{
		vector_push(v,value);
	}
}

void vector_append(vector_t* v,vector_t* u)
{
	if(v->length+u->length>=v->size)vector_resize(v,((v->length+u->length)*3)/2+1);
	memmove(&v->data[v->length],u->data,sizeof(void*)*(u->length));
	v->length+=u->length;
}

void vector_prepend(vector_t* v,vector_t* u)
{
	if(v->length+u->length>=v->size)vector_resize(v,((v->length+u->length)*3)/2+1);
	memmove(&v->data[u->length],v->data,sizeof(void*)*(v->length));
	memmove(v->data,u->data,sizeof(void*)*(u->length));
	v->length+=u->length;
}

void vector_insert_element(vector_t* v, int index, void* value)
{
	if(index<0) vector_unshift(v, value);
	else if(index>=v->length) vector_push(v, value);
	else {
		if(v->length>=v->size)
		{
			vector_resize(v,v->size+v->size/2+1);
		}
		memmove(&v->data[index+1],&v->data[index],sizeof(void*)*(v->length-index-1));
		v->length++;
		v->data[index]=value;
	}
}

void vector_insert(vector_t* v, int index, vector_t* peer)
{
	if(index<0) vector_prepend(v, peer);
	else if(index>=v->length) vector_append(v, peer);
	else {
		if(v->length+peer->length>=v->size)
		{
			vector_resize(v,v->size+peer->size+v->size/2+1);
		}
		memmove(&v->data[index+peer->length],&v->data[index],sizeof(void*)*(v->length-index-1));
		v->length+=peer->length;
		memcpy(&v->data[index],peer->data,peer->length*sizeof(void*));
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
	int i=0;
	int j=v->length-1;
	for(;i<v->length/2;i++)
	{
		void* tmp=v->data[i];
		v->data[i]=v->data[j];
		v->data[j]=tmp;
		j--;
	}
}

void vector_remove(vector_t* v, int from, int to)
{
	if(from<0)from=0;
	if(to>v->length)to=v->length;
	if(to<=from)return;
	if(to!=v->length)
	{
		memmove(&v->data[from],&v->data[to],sizeof(void*)*(to-from));
	}
	v->length-=to-from;
	if(v->length<v->size/2)vector_resize(v,v->length+v->length/2);
}

vector_t* vector_new(int initialSize)
{
	vector_t* v=MALLOC(sizeof(vector_t));
	if(v==NULL)return NULL;
	v->data=NULL;//MALLOC(sizeof(void*)*initialSize);
	v->size=0;
	v->length=0;
	vector_resize(v,initialSize);
	if(v->data==NULL)
	{
		FREE(v);
		return NULL;
	}
	return v;
}

vector_t* vector_subpart(vector_t* v, int from, int to)
{
	if(from<0)from=0;
	if(to>v->length)to=v->length;
	vector_t* output=vector_new(to-from);
	memcpy(output->data,&v->data[from],(to-from)*sizeof(void*));
	output->length=to-from;
	return output;
}

void* vector_get(vector_t* v, int index)
{
	if(index<0 || index>=v->length) warn("vector_get(%d), out-of-bounds", index);
	return v->data[index];
}

void vector_set(vector_t* v, int index, void* value)
{
	if(index<0 || index>=v->length) warn("vector_set(%d, %p), out-of-bounds", index, value);
	v->data[index]=value;
}

vector_t* vector_copy(vector_t* input)
{
	vector_t* output=vector_new(input->length);
	memcpy(output->data,input->data,input->length*sizeof(void*));
	output->length=input->length;
	return output;
}

vector_t* vector_duplicate_content(vector_t* input, int value_size)
{
	vector_t* output=vector_new(input->length);
	int i;
	for(i=0;i<input->length;i++)
	{
		output->data[i]=(void*)MALLOC(value_size);
		memcpy(output->data[i],input->data[i],value_size);
	}
	output->length=input->length;
	return output;
}

void vector_remove_duplicates(vector_t* v)
{
	int i,j=0;
	for(i=0;i<v->length && j<v->length;i++)
	{
		v->data[i]=v->data[j];
		j++;
		while(j<v->length && v->data[i]==v->data[j])j++;
	}
	v->length=i;
}

int vector_search(vector_t* v, void* value)
{
	int i;
	for(i=0;i<v->length;i++)
	{
		if(v->data[i]==value)return i;
	}
	return -1;
}

int vector_search_sorted(vector_t* v, void* value, int (*comparator)(const void*,const void*))
{
	void** found=bsearch(&value,v->data,v->length,sizeof(void*),comparator);
	if(found!=NULL)return found-v->data;
	return -1;
}

void vector_sort(vector_t* v,int (*comparator)(const void*,const void*))
{
	qsort(v->data,v->length,sizeof(void*),comparator);
}

void vector_apply(vector_t* v, void (*callback)(void* data, void* metadata),void* metadata)
{
	int i;
	for(i=0;i<v->length;i++)callback(v->data[i],metadata);
}

void vector_freedata(void* data,void* metadata)
{
	FREE(data);
}
