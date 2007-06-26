#ifndef __UTIL_H__
#define __UTIL_H__

/* The main idea of this function library is to provide perl-like data structures (well, kind of...)
   to have an easier experience in input/output handling. However, the strings are still slow :(
   Some day, I will write a preprocessor which enables to use a subset of the perl styntax sugar/horror in c.
   Mainly written by Benoit Favre -- favre@icsi.berkeley.edu
*/
#define _ISOC99_SOURCE // to get the NAN symbol
#define _GNU_SOURCE // to get vasprintf

#include <errno.h>
#include <string.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

// you dont'want to use the garbage collector => not stable yet
//#define USE_GC

#ifdef USE_GC
#include "gc.h"
#define MALLOC(s) GC_MALLOC(s)
#define REALLOC(p,s) GC_REALLOC(p,s)
#define FREE(p)
#else
#define MALLOC(s) malloc(s)
#define REALLOC(p,s) realloc(p,s)
#define FREE(p) free(p)
#endif

#define debug_level 99
// int debug_level=99;

#define debug(level,format, ...) {if(debug_level>=level)fprintf(stderr, "DEBUG %d: " format ", %s at %s:%d\n", level, ## __VA_ARGS__, errno?strerror(errno):"", __FILE__, __LINE__);}
#define warn(format, ...) {fprintf(stderr, "WARNING: " format ", %s at %s:%d\n", ## __VA_ARGS__, errno?strerror(errno):"", __FILE__, __LINE__);}
#define die(format, ...) {fprintf(stderr, "ERROR: " format ", %s at %s:%d\n", ## __VA_ARGS__, errno?strerror(errno):"", __FILE__, __LINE__);exit(1);}
#define dump(format, ...) {fprintf(stderr, "ERROR: " format ", %s at %s:%d\n", ## __VA_ARGS__, errno?strerror(errno):"", __FILE__, __LINE__);{int* __killer=NULL;*__killer=1;}}

#endif
