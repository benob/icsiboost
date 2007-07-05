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

#include "utils/threads.h"

semaphore_t* semaphore_new(int value)
{
	semaphore_t* output=MALLOC(sizeof(semaphore_t));
	memset(output,55,sizeof(semaphore_t));//DEBUG
	output->count=value;
	pthread_mutexattr_t attributes;
	pthread_mutexattr_init(&attributes);
	pthread_mutexattr_settype(&attributes,PTHREAD_MUTEX_ERRORCHECK);
	pthread_mutex_init(&output->count_mutex,&attributes);
	pthread_mutexattr_destroy(&attributes);
	pthread_cond_init(&output->above_zero,NULL);
	return output;
}

void semaphore_eat(semaphore_t* semaphore)
{
	pthread_mutex_lock(&semaphore->count_mutex);
	while(semaphore->count<=0)
	{
		pthread_cond_wait(&semaphore->above_zero,&semaphore->count_mutex);
	}
	semaphore->count--;
	pthread_mutex_unlock(&semaphore->count_mutex);
}

void semaphore_feed(semaphore_t* semaphore)
{
	pthread_mutex_lock(&semaphore->count_mutex);
	if(semaphore->count==0)
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

