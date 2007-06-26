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

#ifndef _ARRAY_H_
#define _ARRAY_H_

#include "utils/utils.h"
#include "utils/vector.h"

typedef struct arrayelement {
	void* data;
	struct arrayelement* next;
	struct arrayelement* previous;
} arrayelement_t;

typedef struct array {
	arrayelement_t* first;
	arrayelement_t* last;
	int length;
	arrayelement_t* current;
	int current_index;
} array_t;

array_t* array_new();
void array_free(array_t* input);
int array_memory_size(array_t* input);
void array_push(array_t* input, void* data);
void array_unshift(array_t* input, void* data);
void* array_shift(array_t* input);
void* array_pop(array_t* input);
void _array_locate_element(array_t* input, int index);
void* array_get(array_t* input,int index);
void array_set(array_t* input,int index,void* data);
void array_remove(array_t* input, int from, int to);
void* array_remove_element(array_t* input, int index);
array_t* array_copy(array_t* input);
array_t* array_duplicate_content(array_t* input, int value_size);
void array_remove_duplicates(array_t* array);
array_t* array_subpart(array_t* input,int from, int to);
void array_append(array_t* input,array_t* peer);
void array_prepend(array_t* input,array_t* peer);
void array_insert_element(array_t* input, int index, void* value);
void array_insert(array_t* input, int index, array_t* peer);
array_t* array_fusion(array_t* first,array_t* second);
#define vector_to_array(v) array_from_vector(v)
array_t* array_from_vector(vector_t* vector);
#define array_to_vector(a) vector_from_array(a)
vector_t* vector_from_array(array_t* input);
int array_search(array_t* input, void* value);
void array_reverse(array_t* input);
void array_sort(array_t* input, int (*comparator)(const void*,const void*));
void array_apply(array_t* input, void (*callback)(void* data, void* metadata),void* metadata);
void array_freedata(void* data,void* metadata);

//array_iterate_function_and_store(double,values,array,string_t*,string_to_double);
#define array_iterate_and_store(output_type, output_variable, array, array_type, function, ...) { \
	output_type* output_variable=MALLOC(array->length*sizeof(output_type)); \
	int __i; \
	for(__i=0;__i<array->length;__i++) \
	{ \
		output_variable[__i]=function((array_type)array_get(array, __i), ## __VA_ARGS__); \
	} \
}

#endif
