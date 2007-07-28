#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <sys/wait.h>

#include "utils/debug.h"
#include "utils/common.h"

unsigned int _gdb_delay=1;
const char* _gdb_program_name=NULL;
const char* _gdb_commands_file=NULL;

void _sigsegv_handler(int id)
{
	int mypid=getpid();
	signal(SIGSEGV,SIG_DFL);
	int childpid=fork();
	if(childpid==0)
	{
		close(0);
		dup2(2,1);
		char pid[1024];
		snprintf(pid,1024,"%d",mypid);
		execlp("gdb","gdb","-batch","-x",_gdb_commands_file,_gdb_program_name,pid,NULL);
		die("failed to execute \"gdb\"");
		exit(1);
	}
	warn("waiting %d second%s for gdb before dying (you may need to increase this delay)",_gdb_delay,_gdb_delay<2?"":"s");
	sleep(1);
}

void init_debugging(const char* program_name, const char* commands_file, unsigned int delay)
{
	_gdb_program_name=program_name;
	_gdb_commands_file=commands_file;
	_gdb_delay=delay;
	signal(SIGSEGV,_sigsegv_handler);
}
