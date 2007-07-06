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

#ifndef _VECTOR_H_
#define _VECTOR_H_

#include "utils/common.h"

typedef struct vector {
	union {
		void** as_void;
		int32_t* as_int32;
		int64_t* as_int64;
		float* as_float;
		double* as_double;
		char* as_char;
		short* as_short;
	} data;
	size_t length;
	size_t element_size;
	size_t size;
} vector_t;

size_t vector_memory_size(vector_t* input);
void _vector_resize(vector_t* v,size_t newSize);
void vector_optimize(vector_t* v);
void vector_free(vector_t* v);
void vector_push(vector_t* v,void* data);
void vector_push_int32(vector_t* v,int32_t data);
void vector_push_float(vector_t* v,float data);
void* vector_pop(vector_t* v);
void* vector_remove_element(vector_t* v, size_t index);
void* vector_shift(vector_t* v);
void vector_unshift(vector_t* v, void* value);
void vector_append(vector_t* v,vector_t* u);
void vector_prepend(vector_t* v,vector_t* u);
void vector_insert_element(vector_t* v, size_t index, void* value);
void vector_insert(vector_t* v, size_t index, vector_t* peer);
vector_t* vector_fusion(vector_t* v, vector_t* u);
void vector_reverse(vector_t* v);
void vector_remove(vector_t* v, size_t from, size_t to);
vector_t* vector_new(size_t initialSize);
vector_t* vector_new_type(size_t initialSize, size_t element_size);
vector_t* vector_subpart(vector_t* v, size_t from, size_t to);
void* vector_get(vector_t* v, size_t index);
float vector_get_float(vector_t* v, size_t index);
int32_t vector_get_int32(vector_t* v, size_t index);
void vector_set(vector_t* v, size_t index, void* value);
void vector_set_float(vector_t* v, size_t index, float value);
void vector_set_int32(vector_t* v, size_t index, int32_t value);
vector_t* vector_copy(vector_t* input);
vector_t* vector_duplicate_content(vector_t* input, size_t value_size);
void vector_remove_duplicates(vector_t* v);
size_t vector_search(vector_t* v, void* value);
size_t vector_search_sorted(vector_t* v, void* value, int (*comparator)(const void*,const void*));
void vector_sort(vector_t* v,int (*comparator)(const void*,const void*));
void vector_apply(vector_t* v, void (*callback)(void* data, void* metadata),void* metadata);
void vector_freedata(void* data,void* metadata);

#endif
