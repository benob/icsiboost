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

#ifndef _THREADS_H_
#define _THREADS_H_

#include "utils/common.h"

#include <pthread.h>

// a simple portable, pthread based semaphore
typedef struct semaphore {
	int count;
	pthread_mutex_t count_mutex;
	pthread_cond_t above_zero;
} semaphore_t;

semaphore_t* semaphore_new(int value);
void semaphore_eat(semaphore_t* semaphore);
void semaphore_feed(semaphore_t* semaphore);
void semaphore_free(semaphore_t* semaphore);

#endif
