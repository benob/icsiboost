#ifndef _VECTOR_H_
#define _VECTOR_H_

#include "utils/utils.h"

typedef struct vector {
	void** data;
	int length;
	int size;
} vector_t;

int vector_memory_size(vector_t* input);
void vector_resize(vector_t* v,int newSize);
void vector_optimize(vector_t* v);
void vector_free(vector_t* v);
void vector_push(vector_t* v,void* data);
void* vector_pop(vector_t* v);
void* vector_remove_element(vector_t* v, int index);
void* vector_shift(vector_t* v);
void vector_unshift(vector_t* v, void* value);
void vector_append(vector_t* v,vector_t* u);
void vector_prepend(vector_t* v,vector_t* u);
void vector_insert_element(vector_t* v, int index, void* value);
void vector_insert(vector_t* v, int index, vector_t* peer);
vector_t* vector_fusion(vector_t* v, vector_t* u);
void vector_reverse(vector_t* v);
void vector_remove(vector_t* v, int from, int to);
vector_t* vector_new(int initialSize);
vector_t* vector_subpart(vector_t* v, int from, int to);
void* vector_get(vector_t* v, int index);
void vector_set(vector_t* v, int index, void* value);
vector_t* vector_copy(vector_t* input);
vector_t* vector_duplicate_content(vector_t* input, int value_size);
void vector_remove_duplicates(vector_t* v);
int vector_search(vector_t* v, void* value);
int vector_search_sorted(vector_t* v, void* value, int (*comparator)(const void*,const void*));
void vector_sort(vector_t* v,int (*comparator)(const void*,const void*));
void vector_apply(vector_t* v, void (*callback)(void* data, void* metadata),void* metadata);
void vector_freedata(void* data,void* metadata);

#endif
