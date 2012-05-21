/*
 * This file is subject to the terms and conditions defined in
 * file 'COPYING', which is part of this source code package.
 */

#ifndef _THREADS_H_
#define _THREADS_H_

#include "utils/common.h"

#include <pthread.h>

// a simple portable, pthread based semaphore
typedef struct semaphore {
	int count;
	pthread_mutex_t count_mutex;
	pthread_cond_t above_zero;
    int block_on_zero;
} semaphore_t;

semaphore_t* semaphore_new(int value);
int semaphore_eat(semaphore_t* semaphore);
void semaphore_feed(semaphore_t* semaphore);
void semaphore_free(semaphore_t* semaphore);

#endif
