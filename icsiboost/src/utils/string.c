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

#include "string.h"

#include <math.h>

string_t* string_resize(string_t* input,int newSize)
{
	if(newSize==input->size)return input;
	input->data=REALLOC(input->data,newSize);
	input->size=newSize;
	return input;
}

string_t* string_new(const char* string)
{
	int length=strlen(string);
	string_t* output=(string_t*)MALLOC(sizeof(string_t));
#ifdef USE_GC
	output->data=GC_MALLOC_ATOMIC(length+1);
#else
	output->data=MALLOC(length+1);
#endif
	output->length=length;
	output->size=length+1;
	memcpy(output->data,string,length+1);
	return output;
}

string_t* string_new_empty()
{
	int length=0;
	string_t* output=(string_t*)MALLOC(sizeof(string_t));
#ifdef USE_GC
	output->data=GC_MALLOC_ATOMIC(length+1);
#else
	output->data=MALLOC(length+1);
#endif
	output->length=length;
	output->size=length+1;
	*output->data='\0';
	return output;
}

string_t* string_new_from_to(const char* string,int from, int to)
{
	int length=to-from;
	if(length<0)
	{
		length=0;
	}
	string_t* output=(string_t*)MALLOC(sizeof(string_t));
#ifdef USE_GC
	output->data=GC_MALLOC_ATOMIC(length+1);
#else
	output->data=MALLOC(length+1);
#endif
	output->length=length;
	output->size=length+1;
	memcpy(output->data,string+from,length);
	*(output->data+length)='\0';
	return output;
}

string_t* string_copy(string_t* input)
{
	string_t* output=string_new(input->data);
	return output;
}

string_t* string_append_cstr(string_t* input, const char* peer)
{
	int length=strlen(peer);
	string_resize(input,input->length+length+1);
	memcpy(input->data+input->length,peer,length+1);
	input->length+=length;
	return input;
}

string_t* string_prepend_cstr(string_t* input, const char* peer)
{
	int length=strlen(peer);
	string_resize(input,input->length+length+1);
	memmove(input->data+length,input->data,input->length+1);
	memcpy(input->data,peer,length);
	input->length+=length;
	return input;
}

string_t* string_append(string_t* input, string_t* peer)
{
	string_resize(input,input->length+peer->length+1);
	memcpy(input->data+input->length,peer->data,peer->length+1);
	input->length+=peer->length;
	return input;
}

string_t* string_prepend(string_t* input, string_t* peer)
{
	string_resize(input,input->length+peer->length+1);
	memmove(input->data+peer->length,input->data,input->length+1);
	memcpy(input->data,peer->data,peer->length);
	input->length+=peer->length;
	return input;
}

void string_free(string_t* input)
{
	FREE(input->data);
	FREE(input);
}

string_t* string_substr(string_t* input,int from,int to)
{
	if(to<from || from<0 || to>input->length)return NULL;
	string_t* output=(string_t*)MALLOC(sizeof(string_t));
#ifdef USE_GC
	output->data=GC_MALLOC_ATOMIC(to-from+1);
#else
	output->data=MALLOC(to-from+1);
#endif
	output->length=to-from;
	output->size=to-from+1;
	memcpy(output->data,input->data+from,to-from);
	*(output->data+output->length)='\0';
	return output;
}

string_t* string_reverse(string_t* input)
{
	int i=0;
	int j=input->length-1;
	for(;i<input->length/2;i++)
	{
		char tmp=input->data[i];
		input->data[i]=input->data[j];
		input->data[j]=tmp;
		j--;
	}
	return input;
}

string_t* string_chomp(string_t* input)
{
	int i=input->length;
	while(i>0 && (input->data[i]=='\n' || input->data[i]=='\r'))i--;
	if(i>0 && i<input->length)input->data[i+1]='\0';
	input->length=i;
	return input;
}

string_t* string_join(string_t* separator, array_t* parts)
{
	if(parts->first==NULL)return string_new("");
	arrayelement_t* current=parts->first;
	// compute length
	int length=((string_t*)current->data)->length;
	current=current->next;
	do
	{
		length+=separator->length;
		length+=((string_t*)current->data)->length;
		current=current->next;
	}
	while(current);
	current=parts->first;
	string_t* output=string_copy((string_t*)current->data);
	string_resize(output,length+1);
	char* output_cstr=output->data+output->length;
	current=current->next;
	while(current)
	{
		memcpy(output_cstr,separator->data,separator->length);
		output_cstr+=separator->length;
		string_t* current_string=(string_t*)current->data;
		memcpy(output_cstr,current_string->data,current_string->length);
		output_cstr+=current_string->length;
		current=current->next;
	}
	*output_cstr='\0';
	output->length=length;
	return output;
}

string_t* string_join_cstr(const char* separator, array_t* parts)
{
	string_t* separator_string=string_new(separator);
	string_t* output=string_join(separator_string,parts);
	string_free(separator_string);
	return output;
}

void regexstatus_free(regexstatus_t* status)
{
	regfree(&status->expression);
	vector_free(status->groups);
	FREE(status);
}

regexstatus_t* string_match(string_t* input, const char* pattern, int flags, regexstatus_t* status)
{
	if(status==NULL)
	{
		status=(regexstatus_t*)MALLOC(sizeof(regexstatus_t));
		if(regcomp(&status->expression,pattern,flags|REG_EXTENDED)!=0) warn("compiling regex /%s/",pattern);
		status->expression.re_nsub++;
		status->groups=vector_new(status->expression.re_nsub);
		status->groups->length=status->expression.re_nsub;
		status->start=0;
		status->end=0;
	} else {
		flags|=REG_NOTBOL;
	}
	flags|=REG_STARTEND;
	regmatch_t match[status->expression.re_nsub];
	match[0].rm_so=status->end;
	match[0].rm_eo=input->length;
	int result=regexec(&status->expression,input->data,status->expression.re_nsub,match,flags);
	if(result==0)
	{
		int i=0;
		for(i=0;i<status->expression.re_nsub;i++)
		{
			status->groups->data[i]=string_new_from_to(input->data,match[i].rm_so,match[i].rm_eo);
		}
		status->start=match[0].rm_so;
		status->end=match[0].rm_eo;
		return status;
	}
	regexstatus_free(status);
	return NULL;
}

array_t* string_split(const char* separator, string_t* input)
{
	array_t* output=array_new();
	regex_t expression;
	if(regcomp(&expression,separator,REG_EXTENDED)!=0) warn("compiling regex /%s/",separator);
	expression.re_nsub++;
	regmatch_t match[expression.re_nsub];
	match[0].rm_so=0;
	match[0].rm_eo=input->length;
	regoff_t last_start=0;
	while(regexec(&expression,input->data,expression.re_nsub,match,REG_STARTEND)==0)
	{
		if(match[0].rm_so!=0)array_push(output,string_new_from_to(input->data,last_start,match[0].rm_so));
		last_start=match[0].rm_eo;
		match[0].rm_so=match[0].rm_eo;
		match[0].rm_eo=input->length;
	}
	if(last_start!=input->length)array_push(output,string_new_from_to(input->data,last_start,input->length));
	regfree(&expression);
	return output;
}

// WARNING: $1, $2 ... don't get replaced
int string_replace(string_t* input, const char* pattern, string_t* replacement, int flags)
{
	regex_t expression;
	if(regcomp(&expression,pattern,flags|REG_EXTENDED)!=0) warn("compiling regex /%s/",pattern);
	expression.re_nsub++;
	regmatch_t match[expression.re_nsub];
	match[0].rm_so=0;
	match[0].rm_eo=input->length;
	regoff_t last_start=0;
	array_t* output=array_new();
	while(regexec(&expression,input->data,expression.re_nsub,match,REG_STARTEND)==0)
	{
		if(match[0].rm_so!=0)array_push(output,string_new_from_to(input->data,last_start,match[0].rm_so));
		array_push(output,string_copy(replacement));
		last_start=match[0].rm_eo;
		match[0].rm_so=match[0].rm_eo;
		match[0].rm_eo=input->length;
	}
	if(last_start!=input->length)array_push(output,string_new_from_to(input->data,last_start,input->length));
	regfree(&expression);
	string_t* joint_output=string_join_cstr("",output);
	int i;
	for(i=0;i<output->length;i++)string_free((string_t*)array_get(output,i));
	array_free(output);
	FREE(input->data);
	input->data=joint_output->data;
	input->length=joint_output->length;
	input->size=joint_output->size;
	FREE(joint_output);
	return 1;
}

void string_array_free(array_t* input)
{
	int i;
	for(i=0;i<input->length;i++)
	{
		string_free((string_t*)array_get(input,i));
	}
	array_free(input);
}

void string_vector_free(vector_t* input)
{
	int i;
	for(i=0;i<input->length;i++)
	{
		string_free((string_t*)input->data[i]);
	}
	vector_free(input);
}

int string_to_int(string_t* input)
{
	char* error=NULL;
	int output=strtol(input->data,&error,10);
	if(error!=NULL && *error=='\0')return output;
	warn("string_to_int(%s) failed",input->data);
	return 0;
}

double string_to_double(string_t* input)
{
	char* error=NULL;
	double output=strtod(input->data,&error);
	if(error!=NULL && *error=='\0')return output;
	warn("string_to_double(%s) failed",input->data);
	return NAN;
}

float string_to_float(string_t* input)
{
	char* error=NULL;
	float output=strtof(input->data,&error);
	if(error!=NULL && *error=='\0')return output;
	warn("string_to_float(%s) failed",input->data);
	return 0;
}

// faster without GC which forces to duplicate memory
string_t* string_sprintf(const char* format, ...)
{
	string_t* output=(string_t*)MALLOC(sizeof(string_t));
	int result=0;
	va_list argument_list;
	va_start(argument_list, format);
#ifdef USE_GC
	char* temp;
	result=vasprintf(&temp, format, argument_list);
	output->data=GC_STRDUP(temp);
	free(temp);
#else
	result=vasprintf(&output->data, format, argument_list);
#endif
	va_end(argument_list);
	if(result==-1 || output->data==NULL)warn("string_sprintf() failed");
	output->length=strlen(output->data);
	output->size=output->length+1;
	return output;
}

void string_array_fprintf(FILE* stream, const char* format, array_t* array)
{
	int i;
	for(i=0;i<array->length;i++)
	{
		string_t* string=array_get(array, i);
		fprintf(stream, format, string->data);
	}
}

array_t* string_argv_to_array(int argc, char** argv)
{
	array_t* output=array_new();
	int i;	
	for(i=1; i<argc; i++)
	{
		array_push(output,string_new(argv[i]));
	}
	return output;
}

int string_compare(string_t* a, string_t* b)
{
	return strcmp(a->data, b->data);
}

int string_equal(string_t* a, string_t* b)
{
	return strcmp(a->data, b->data)==0;
}

int string_not_equal(string_t* a, string_t* b)
{
	return strcmp(a->data, b->data)!=0;
}

int string_compare_cstr(string_t* a, const char* b)
{
	return strcmp(a->data, b);
}

int string_equal_cstr(string_t* a, const char* b)
{
	return strcmp(a->data, b)==0;
}

int string_not_equal_cstr(string_t* a, const char* b)
{
	return strcmp(a->data, b)!=0;
}

