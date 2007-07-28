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

#include "array.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

array_t* array_new()
{
	array_t* output=MALLOC(sizeof(array_t));
	output->first=NULL;
	output->last=NULL;
	output->length=0;
	output->current=NULL;
	output->current_index=-1;
	return output;
}

void array_free(array_t* input)
{
	arrayelement_t* element=input->first;
	while(element!=NULL)
	{
		arrayelement_t* tmp=element->next;
		FREE(element);
		element=tmp;
	}
	FREE(input);
}

size_t array_memory_size(array_t* input)
{
	size_t size=sizeof(array_t);
	size+=input->length*(sizeof(arrayelement_t));
	return size;
}

void array_push(array_t* input, void* data)
{
	arrayelement_t* element=MALLOC(sizeof(arrayelement_t));
	element->data=data;
	element->next=NULL;
	element->previous=input->last;
	if(input->last)input->last->next=element;
	else input->first=element;
	input->last=element;
	input->length++;
	input->current=element;
	input->current_index=input->length-1;
}

void array_unshift(array_t* input, void* data)
{
	arrayelement_t* element=MALLOC(sizeof(arrayelement_t));
	element->data=data;
	element->next=input->first;
	if(input->first)input->first->previous=element;
	else input->last=element;
	input->first=element;
	input->length++;
	input->current=element;
	input->current_index=0;
}

void* array_shift(array_t* input)
{
	if(!input->first)return NULL;
	input->length--;
	arrayelement_t* element=input->first;
	void* output=element->data;
	input->first=element->next;
	if(input->first)input->first->previous=NULL;
	else input->last=NULL;
	input->current=input->first;
	input->current_index=0;
	FREE(element);
	return output;
}

void* array_pop(array_t* input)
{
	if(!input->last)return NULL;
	input->length--;
	arrayelement_t* element=input->last;
	void* output=element->data;
	input->last=element->previous;
	if(input->last)input->last->next=NULL;
	else input->first=NULL;
	input->current=input->last;
	input->current_index=input->length-1;
	FREE(element);
	return output;
}

void _array_locate_element(array_t* input, size_t index)
{
	if(index<0 || index>=input->length)
	{
		warn("_array_locate_element(%p,%zd), out-of-bounds", input, index);
		return;
	}
	if(index<input->current_index && input->current_index-index>index)
	{
		input->current=input->first;
		input->current_index=0;
	}
	else if(index>input->current_index && index-input->current_index>input->length-index-1)
	{
		input->current=input->last;
		input->current_index=input->length-1;
	}
	arrayelement_t* element=input->current;
	size_t i=input->current_index;
	while(i!=index)
	{
		if(i<index)
		{
			element=element->next;
			i++;
		}
		else
		{
			element=element->previous;
			i--;
		}
	}
	input->current=element;
	input->current_index=index;
}

void* array_get(array_t* input,size_t index)
{
	_array_locate_element(input,index);
	return input->current->data;
}

void array_set(array_t* input,size_t index,void* data)
{
	_array_locate_element(input,index);
	input->current->data=data;
}

void array_remove(array_t* input, size_t from, size_t to)
{
	if(to<=from)to=from+1;
	if(to>input->length)to=input->length;
	_array_locate_element(input,from);
	arrayelement_t* previous=input->current->previous;
	while(input->current && input->current_index<to)
	{
		arrayelement_t* removed=input->current;
		input->current=input->current->next;
		input->current_index++;
		FREE(removed);
	}
	if(input->current==NULL)
	{
		input->last=previous;
	}
	if(previous==NULL)
	{
		input->first=input->current;
	}
	else
	{
		previous->next=input->current;
	}
	if(input->current!=NULL)
	{
		input->current->previous=previous;
	}
	input->current=input->first;
	input->current_index=0;
	input->length-=(to-from);
}

void* array_remove_element(array_t* input, size_t index)
{
	void* value=array_get(input,index);
	array_remove(input,index,index+1);
	return value;
}

array_t* array_copy(array_t* input)
{
	array_t* output=array_new();
	arrayelement_t* element=input->first;
	while(element)
	{
		array_push(output,element->data);
		element=element->next;
	}
	return output;
}

array_t* array_duplicate_content(array_t* input, size_t value_size)
{
	array_t* output=array_copy(input);
	arrayelement_t* element=output->first;
	while(element)
	{
		void* data=element->data;
		element->data=MALLOC(value_size);
		memcpy(element->data,data,value_size);
		element=element->next;
	}
	return output;
}

void array_remove_duplicates(array_t* array)
{
	arrayelement_t* element=array->first;
	size_t i=0;
	while(element)
	{
		while(element->next && element->next->data==element->data)
		{
			array_remove_element(array,i+1);
		}
		element=element->next;
		i++;
	}
}

array_t* array_subpart(array_t* input,size_t from, size_t to)
{
	if(from<0)from=0;
	if(to>input->length)to=input->length;
	_array_locate_element(input,from);
	array_t* output=array_new();
	while(input->current && input->current_index<to)
	{
		array_push(output,input->current->data);
		input->current=input->current->next;
		input->current_index++;
	}
	return output;
}

void array_append(array_t* input,array_t* peer)
{
	arrayelement_t* element=peer->first;
	while(element)
	{
		array_push(input,element->data);
		element=element->next;
	}
}

void array_prepend(array_t* input,array_t* peer)
{
	arrayelement_t* element=peer->first;
	while(element)
	{
		array_unshift(input,element->data);
		element=element->next;
	}
}

void array_insert_element(array_t* input, size_t index, void* value)
{
	if(index<0) array_unshift(input, value);
	else if(index>=input->length) array_push(input, value);
	else {
		_array_locate_element(input, index);
		arrayelement_t* element=MALLOC(sizeof(arrayelement_t));
		element->previous=input->current->previous;
		element->data=value;
		element->next=input->current;
		input->current->previous->next=element;
		input->current->previous=element;
		input->length++;
	}
}

void array_insert(array_t* input, size_t index, array_t* peer)
{
	if(index<0) array_prepend(input, peer);
	else if(index>=input->length) array_append(input, peer);
	else {
		arrayelement_t* element=peer->last;
		while(element)
		{
			array_insert_element(input, index, element->data);
			element=element->previous;
		}
	}
}

array_t* array_fusion(array_t* first,array_t* second)
{
	arrayelement_t* left_element=first->last;
	arrayelement_t* right_element=second->first;
	if(left_element==NULL)
	{
		FREE(first);
		return second;
	}
	if(right_element==NULL)
	{
		FREE(second);
		return first;
	}
	left_element->next=right_element;
	right_element->previous=left_element;
	first->last=second->last;
	first->current=left_element;
	first->current_index=first->length;
	first->length+=second->length;
	FREE(second);
	return first;
}

array_t* array_from_vector(vector_t* vector)
{
	size_t i;
	array_t* output=array_new();
	for(i=0;i<vector->length;i++)
	{
		array_push(output,vector_get(vector,i));
	}
	return output;
}

vector_t* vector_from_array(array_t* input)
{
	vector_t* output=vector_new(input->length);
	output->length=input->length;
	arrayelement_t* element=input->first;
	size_t i=0;
	while(element)
	{
		vector_set(output,i,element->data);
		element=element->next;
		i++;
	}
	output->length=input->length;
	return output;
}

size_t array_search(array_t* input, void* value)
{
	input->current=input->first;
	input->current_index=0;
	while(input->current)
	{
		if(input->current->data==value)return input->current_index;
		input->current=input->current->next;
		input->current_index++;
	}
	input->current=input->last;
	input->current_index=input->length-1;
	return -1;
}

void array_reverse(array_t* input)
{
	if(input->length==0)return;
	arrayelement_t* element=input->first;
	while(element)
	{
		arrayelement_t* next=element->next;
		element->next=element->previous;
		element->previous=next;
		element=next;
	}
	element=input->first;
	input->first=input->last;
	input->last=element;
	input->first->previous=NULL;
	input->last->next=NULL;
	input->current=input->first;
	input->current_index=0;
	element=input->first;
}

// merge sort
void array_sort(array_t* input, int (*comparator)(const void*,const void*))
{
	size_t pass=1;
	if(input->first==NULL || input->first->next==NULL)return;
	while(pass<input->length)
	{
		arrayelement_t* current=input->first;
		arrayelement_t* output=NULL;
		while(current)
		{
			size_t i,j;
			arrayelement_t* first=current;
			arrayelement_t* second=first;
			for(i=0;second && i<pass;i++)second=second->next;
			for(i=0,j=0;first && second && j<pass && i<pass;)
			{
				if(comparator(&first->data,&second->data)<0)
				{
					first->previous=output;
					output=first;
					first=first->next;
					i++;
					continue;
				}
				second->previous=output;
				output=second;
				second=second->next;
				j++;
			}
			for(;first && i<pass;i++)
			{
				first->previous=output;
				output=first;
				first=first->next;
			}
			for(;second && j<pass;j++)
			{
				second->previous=output;
				output=second;
				second=second->next;
			}
			current=second;
		}
		input->last=output;
		while(output->previous)
		{
			output->previous->next=output;
			output=output->previous;
		}
		input->first=output;
		input->last->next=NULL;
		input->first->previous=NULL;
		pass*=2;
	}
	input->current=input->first;
	input->current_index=0;
}

void array_apply(array_t* input, void (*callback)(void* data, void* metadata),void* metadata)
{
	arrayelement_t* element=input->first;
	while(element)
	{
		callback(&element->data,metadata);
		element=element->next;
	}
}

void array_freedata(void* data,void* metadata)
{
	FREE(data);
}

