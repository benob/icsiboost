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
	void* data;
	size_t length;
	size_t element_size;
	size_t size;
} vector_t;

// functions that deal with the typed value
vector_t* _vector_new(size_t initialSize, size_t element_size);
void _vector_push(vector_t* v, void* data);
void* _vector_pop(vector_t* v);
void* _vector_remove_element(vector_t* v, size_t index);
void* _vector_shift(vector_t* v);
void _vector_unshift(vector_t* v, void* value);
size_t _vector_search(vector_t* v, void* value);
size_t _vector_search_sorted(vector_t* v, void* value, int (*comparator)(const void*,const void*));
void* _vector_get(vector_t* v, size_t index);
void _vector_set(vector_t* v, size_t index, void* value);
void _vector_insert_element(vector_t* v, int index, void* value);

#define vector_implement_functions_for_type(type,null_value) \
	vector_t* vector_new_##type(size_t initialSize) { return _vector_new(initialSize,sizeof(type)); } \
	void vector_push_##type(vector_t* v, type value) { _vector_push(v,&value); } \
	type vector_pop_##type(vector_t* v) { type* output=(type*) _vector_pop(v); return output==NULL?null_value:*output; } \
	type vector_remove_element_##type(vector_t* v, size_t index) { type* output=(type*) _vector_remove_element(v, index); return output==NULL?null_value:*output; } \
	type vector_shift_##type(vector_t* v) { type* output=(type*) _vector_shift(v); return output==NULL?null_value:*output; } \
	void vector_unshift_##type(vector_t* v, type value) { _vector_unshift(v, &value); } \
	size_t vector_search_##type(vector_t* v, type value) { return _vector_search(v ,&value); } \
	size_t vector_search_sorted_##type(vector_t* v, type value, int (*comparator)(const void*,const void*)) { return _vector_search_sorted(v, &value, comparator); } \
	type vector_get_##type(vector_t* v, size_t index) { return *(type*) _vector_get(v, index); } \
	void vector_set_##type(vector_t* v, size_t index, type value) { _vector_set(v, index, &value); } \
	void vector_insert_element_##type(vector_t* v, int index, type value) { _vector_insert_element(v, index, &value); }

/*#define vector_declare_functions_for_type(type,null_value) \
	vector_t* vector_new_##type(size_t initialSize); \
	void vector_push_##type(vector_t* v, type value); \
	type vector_pop_##type(vector_t* v); \
	type vector_remove_element_##type(vector_t* v, size_t index); \
	type vector_shift_##type(vector_t* v); \
	void vector_unshift_##type(vector_t* v, type value); \
	size_t vector_search_##type(vector_t* v, type value); \
	size_t vector_search_sorted_##type(vector_t* v, type value, int (*comparator)(const void*,const void*)); \
	type vector_get_##type(vector_t* v, size_t index); \
	void vector_set_##type(vector_t* v, size_t index, type value); \
	void vector_insert_element_##type(vector_t* v, int index, type value); \
	*/

/*vector_declare_functions_for_type(float, 0.0);
vector_declare_functions_for_type(double, 0.0);
vector_declare_functions_for_type(int32_t, 0);
vector_declare_functions_for_type(int64_t, 0);*/

// declare functions for void*
vector_t* vector_new(size_t initialSize);
void vector_push(vector_t* v, void* value);
void* vector_pop(vector_t* v);
void* vector_remove_element(vector_t* v, size_t index);
void* vector_shift(vector_t* v);
void vector_unshift(vector_t* v, void* value);
size_t vector_search(vector_t* v, void* value);
size_t vector_search_sorted(vector_t* v, void* value, int (*comparator)(const void*,const void*));
void* vector_get(vector_t* v, size_t index);
void vector_set(vector_t* v, size_t index, void* value);
void vector_insert_element(vector_t* v, int index, void* value);

// generic vector functions
size_t vector_memory_size(vector_t* input);
void _vector_resize(vector_t* v,size_t newSize);
void vector_optimize(vector_t* v);
void vector_free(vector_t* v);
void vector_append(vector_t* v,vector_t* u);
void vector_prepend(vector_t* v,vector_t* u);
void vector_insert(vector_t* v, size_t index, vector_t* peer);
vector_t* vector_fusion(vector_t* v, vector_t* u);
void vector_remove(vector_t* v, size_t from, size_t to);
vector_t* vector_subpart(vector_t* v, size_t from, size_t to);
vector_t* vector_copy(vector_t* input);
vector_t* vector_copy_and_duplicate_pointers(vector_t* input, size_t value_size);
void vector_reverse(vector_t* v);
void vector_remove_duplicates(vector_t* v);
void vector_sort(vector_t* v,int (*comparator)(const void*,const void*));
void vector_apply(vector_t* v, void (*callback)(void* data, void* metadata),void* metadata);
void vector_freedata(void* data,void* metadata);

#endif
