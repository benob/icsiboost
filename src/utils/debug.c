/*
 * This file is subject to the terms and conditions defined in
 * file 'COPYING', which is part of this source code package.
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <sys/wait.h>

#include "utils/common.h"
#include "utils/debug.h"

unsigned int _debug_delay=1;
int _debug_interactive=0;
const char* _debug_program_name=NULL;

void _sigsegv_handler(int id)
{
	warn("receved signal %d",id);
	int mypid=getpid();
	signal(id,SIG_DFL);
	int childpid=fork();
	if(childpid==0)
	{
		if(_debug_interactive==DEBUG_NON_INTERACTIVE)
		{
			int filedes[2];
			if(pipe(filedes)==-1)
			{
				warn("could not enable non-interactive stack trace");
			}
			close(0);
			dup2(filedes[0],0);
			char* commands="set prompt\ncontinue\nwhere\nquit\ny\n";
			write(filedes[1],commands,strlen(commands));
			//				close(filedes[1]);
		}
		dup2(2,1);
		char pid[1024];
		snprintf(pid,1024,"%d",mypid);
		execlp("gdb","gdb","-q",_debug_program_name,pid,NULL);
		die("failed to execute \"gdb\", you need to debug manually");
		exit(1);
	}
	warn("waiting %d second%s for gdb before dying (you may need to increase this delay)",_debug_delay,_debug_delay<2?"":"s");
	sleep(_debug_delay);
}

void init_debugging(const char* program_name, int interactive)
{
	_debug_program_name=program_name;
	//_debug_delay=1;
	_debug_interactive=interactive;
	signal(SIGSEGV,_sigsegv_handler);
	signal(SIGBUS,_sigsegv_handler);
}
