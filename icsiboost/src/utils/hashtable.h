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

#ifndef _HASHTABLE_H_
#define _HASHTABLE_H_

#define HASHTABLE_REORDER_ON_ACCESS // put last accessed element at the head of buckets (faster for word accesses)
//#define HASHTABLE_GATHER_STATS      // compute the real number of collisions per access
#define HASHTABLE_INCREASE_SIZE     // increase number of buckets to limit the average number of collisions
//#define HASHTABLE_CENTRALIZE_KEYS   // centralize hash keys to reduce the size of small redundant tables (a la perl)

/*#define _GNU_SOURCE
#define _LARGE_FILE_SOURCE
#define _FILE_OFFSET_BITS 64 */

#include "utils/common.h"
#include "utils/vector.h"
#include "utils/mapped.h"

#include <sys/types.h>
#include <stdint.h>
//#include <idn-int.h>

typedef struct hashelement {
	void* key;
	size_t key_length;
	void* value;
	struct hashelement* next_in_bucket;
	uint32_t hashcode;
} hashelement_t;

typedef struct hashtable {
	size_t size;       // number of buckets
	hashelement_t** buckets;
	size_t length;     // number of elements
	hashelement_t* current_element;
#ifdef HASHTABLE_INCREASE_SIZE
	size_t max_average_collisions;
	double resize_factor;
#endif
#ifdef HASHTABLE_GATHER_STATS
	unsigned int num_accesses;
	unsigned int num_hits_per_access;
#endif
} hashtable_t;

uint32_t _hashtable_function(void* key,size_t key_length);
int _hashtable_key_equals(void* key1, size_t key1_length, void* key2, size_t key2_length);
#define hashtable_new() hashtable_new_size_collisions_factor(16, 5, 2)
#define hashtable_new_size(size) hashtable_new_size_collisions_factor(size, 5, 2)
#define hashtable_new_size_collisions(size,collisions) hashtable_new_size_collisions_factor(size, collisions, 2)
hashtable_t* hashtable_new_size_collisions_factor(size_t size, size_t max_avg_collisions, double resize_factor);
#define hashtable_store(h,key,key_length,value) hashtable_set(h,key,key_length,value)
#define hashtable_put(h,key,key_length,value) hashtable_set(h,key,key_length,value)
void hashtable_set(hashtable_t* h,void* key,size_t key_length,void* value);
#define hashtable_delete(h, key, key_length) hashtable_remove(h, key, key_length)
void* hashtable_remove(hashtable_t* h,void* key,size_t key_length);
#define hashtable_exists(h,key,key_length) hashtable_get(h,key,key_length)
#define hashtable_fetch(h,key,key_length) hashtable_get(h,key,key_length)
void* hashtable_get(hashtable_t* h,void* key,size_t key_length);
void* hashtable_get_or_default(hashtable_t* h,void* key,size_t key_length,void* defaultValue);
off_t hashtable_get_from_file(FILE* file,void* key,size_t key_length);
off_t hashtable_get_from_mapped(mapped_t* mapped,void* key,size_t key_length);
void _hashtable_freeelement(void* data, void* metadata);
void hashtable_free(hashtable_t* h);
void hashtable_optimize(hashtable_t* h);//, int (*compare)(const void* a,const void* b))
size_t hashtable_memory_size(hashtable_t* h);
void hashtable_stats(hashtable_t* h,FILE* stream);
int hashtable_save(hashtable_t* h,FILE* file, off_t (*saveValue)(hashelement_t* element,void* metadata), void* metadata);
hashtable_t* hashtable_load(FILE* file, void* (*loadValue)(void* key,size_t key_length,off_t location,void* metadata),void* metadata);
void hashtable_apply(hashtable_t* h, void (*callback)(hashelement_t* element, void* metadata),void* metadata);
vector_t* hashtable_elements(hashtable_t* h);
vector_t* hashtable_keys(hashtable_t* h);
vector_t* hashtable_values(hashtable_t* h);
hashelement_t* hashtable_first_element(hashtable_t* h);
hashelement_t* hashtable_next_element(hashtable_t* h);
void* hashtable_first_key(hashtable_t* h);
void* hashtable_next_key(hashtable_t* h);
void* hashtable_first_value(hashtable_t* h);
void* hashtable_next_value(hashtable_t* h);
void hashtable_freevalue(hashelement_t* element, void* metadata);
void _hashtable_replicate(hashelement_t* element, void* metadata);
void hashtable_resize(hashtable_t* h,size_t newSize);

#endif
