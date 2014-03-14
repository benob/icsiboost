/*
 * This file is subject to the terms and conditions defined in
 * file 'COPYING', which is part of this source code package.
 */

#include "utils/threads.h"

semaphore_t* semaphore_new(int value)
{
	semaphore_t* output=MALLOC(sizeof(semaphore_t));
	memset(output,55,sizeof(semaphore_t));//DEBUG
	output->count=value;
    output->block_on_zero = 1;
	pthread_mutexattr_t attributes;
	pthread_mutexattr_init(&attributes);
	pthread_mutexattr_settype(&attributes,PTHREAD_MUTEX_ERRORCHECK);
	pthread_mutex_init(&output->count_mutex,&attributes);
	pthread_mutexattr_destroy(&attributes);
	pthread_cond_init(&output->above_zero,NULL);
	return output;
}

int semaphore_eat(semaphore_t* semaphore)
{
	pthread_mutex_lock(&semaphore->count_mutex);
    int old_count = semaphore->count;
	while(semaphore->count<=0 && semaphore->block_on_zero)
	{
		pthread_cond_wait(&semaphore->above_zero,&semaphore->count_mutex);
	}
	semaphore->count--;
	pthread_mutex_unlock(&semaphore->count_mutex);
    return old_count;
}

void semaphore_feed(semaphore_t* semaphore)
{
	pthread_mutex_lock(&semaphore->count_mutex);
	if(semaphore->count==0 && semaphore->block_on_zero)
	{
		pthread_cond_broadcast(&semaphore->above_zero);
	}
	semaphore->count++;
	pthread_mutex_unlock(&semaphore->count_mutex);
}

void semaphore_free(semaphore_t* semaphore)
{
	pthread_cond_destroy(&semaphore->above_zero);
	pthread_mutex_destroy(&semaphore->count_mutex);
	FREE(semaphore);
}

