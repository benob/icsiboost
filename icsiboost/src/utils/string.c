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
#include "utils/hashtable.h"

#include <math.h>

string_t* string_resize(string_t* input,size_t newSize)
{
	if(newSize==input->size)return input;
	input->data=REALLOC(input->data,newSize);
	input->size=newSize;
	return input;
}

string_t* string_new(const char* string)
{
	size_t length=strlen(string);
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
	size_t length=0;
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

string_t* string_new_from_to(const char* string,size_t from, size_t to)
{
	size_t length=to-from;
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
	size_t length=strlen(peer);
	string_resize(input,input->length+length+1);
	memcpy(input->data+input->length,peer,length+1);
	input->length+=length;
	return input;
}

string_t* string_prepend_cstr(string_t* input, const char* peer)
{
	size_t length=strlen(peer);
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

string_t* string_substr(string_t* input,size_t from,size_t to)
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
	size_t i=0;
	size_t j=input->length-1;
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
	size_t i=input->length;
	while(i>0 && (input->data[i-1]=='\n' || input->data[i-1]=='\r'))i--;
	if(i>0 && i<input->length)input->data[i]='\0';
	input->length=i;
	return input;
}

array_t* string_array_chomp(array_t* input)
{
	int i=0;
	for(i=0; i<input->length; i++)
		string_chomp((string_t*)array_get(input, i));
	return input;
}

string_t* string_join(string_t* separator, array_t* parts)
{
	if(parts->first==NULL)return string_new("");
	arrayelement_t* current=parts->first;
	// compute length
	size_t length=((string_t*)current->data)->length;
	current=current->next;
	while(current)
	{
		length+=separator->length;
		length+=((string_t*)current->data)->length;
		current=current->next;
	}
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

#ifdef USE_PCRE

typedef struct regexstatus {
	pcre* expression;
	//vector_t* groups;
	size_t start;
	size_t end;
	int flags;
	int num_subs;
	const char* replacement;
	vector_t* between_replacements;
	vector_t* replacement_mapping;
} regexstatus_t;

#define REGEX_FLAG_NONE 0
#define REGEX_FLAG_GLOBAL (1<<0)
#define REGEX_FLAG_CONTINUE (1<<1)
#define REGEX_FLAG_CASE_INSENSITIVE (1<<2)
#define REGEX_FLAG_MULTILINE (1<<3)
#define REGEX_FLAG_ONCE (1<<4)
#define REGEX_FLAG_NOSUB (1<<5)
#define REGEX_FLAG_REVERSE (1<<6)

void _regexstatus_free(regexstatus_t* status)
{
	if(status->replacement_mapping!=NULL)vector_free(status->replacement_mapping);
	if(status->between_replacements!=NULL)string_vector_free(status->between_replacements);
	pcre_free(status->expression);
	FREE(status);
}

#ifdef STRING_REGEX_USE_CACHE
hashtable_t* _regex_cache=NULL;
#endif

regexstatus_t* _regexstatus_new(const char* pattern, const char* replacement, const char* flags_string)
{
	regexstatus_t* status=NULL;
	int flags=REGEX_FLAG_NONE;
	if(flags_string!=NULL)
	{
		const char* current_flag=flags_string;
		while(current_flag && *current_flag!='\0')
		{
			switch(*current_flag) {
				case 'g': flags|=REGEX_FLAG_GLOBAL;break;
				case 'c': flags|=REGEX_FLAG_CONTINUE;break;
				case 'i': flags|=REGEX_FLAG_CASE_INSENSITIVE;break;
				case 'm': flags|=REGEX_FLAG_MULTILINE;break;
				case 'o': flags|=REGEX_FLAG_ONCE;break;
				case 'n': flags|=REGEX_FLAG_NOSUB;break;
				case '!': flags|=REGEX_FLAG_REVERSE;break;
				default:
					if(replacement==NULL) {
						die("invlid flag \"%c\", in m/%s/%s",*current_flag, pattern, flags_string);
					} else {
						die("invlid flag \"%c\", in s/%s/%s/%s",*current_flag, pattern, replacement, flags_string);
					}
					break;
			}
			current_flag++;
		}
	}
#ifdef STRING_REGEX_USE_CACHE
	if((flags & REGEX_FLAG_ONCE) == 0)
	{
		if(_regex_cache==NULL)
		{
			_regex_cache=hashtable_new();
		}
		else
		{
			status=hashtable_get(_regex_cache, pattern, strlen(pattern));
		}
	}
#endif
	if(status==NULL || ((status->flags & (REGEX_FLAG_CASE_INSENSITIVE|REGEX_FLAG_MULTILINE|REGEX_FLAG_NOSUB)) != 
			(flags & (REGEX_FLAG_CASE_INSENSITIVE|REGEX_FLAG_MULTILINE|REGEX_FLAG_NOSUB))))
	{
		if(status==NULL)
		{
			status=MALLOC(sizeof(regexstatus_t));
			memset(status,0,sizeof(regexstatus_t));
#ifdef STRING_REGEX_USE_CACHE
			if((flags & REGEX_FLAG_ONCE) == 0)hashtable_set(_regex_cache, pattern, strlen(pattern), status);
#endif
		}
		else
		{
			pcre_free(status->expression);
		}
		status->flags=flags;
		const char* error_message;
		int error_offset;
		status->expression = pcre_compile(pattern, 
			(flags & REGEX_FLAG_CASE_INSENSITIVE ? PCRE_CASELESS : 0) |
			(flags & REGEX_FLAG_MULTILINE ? PCRE_MULTILINE : 0) |
			(flags & REGEX_FLAG_NOSUB ? PCRE_NO_AUTO_CAPTURE : 0) | 0,
			&error_message, &error_offset, NULL);
		if(status->expression==NULL)
		{
			if(replacement==NULL)
			{ 
				die("compiling regex m/%s/%s, at char %d, %s", pattern, flags_string, error_offset, error_message);
			} else {
				die("compiling regex s/%s/%s/%s, at char %d, %s", pattern, replacement, flags_string, error_offset, error_message);
			}
		}
		if(pcre_fullinfo(status->expression, NULL, PCRE_INFO_CAPTURECOUNT, &status->num_subs)!=0)
			die("could not get number of groups in regex /%s/", pattern);
		status->num_subs++;
	}
	if(replacement!=NULL && (status->replacement==NULL || strcmp(status->replacement,replacement)!=0))
	{
		status->replacement=replacement;
		status->between_replacements=vector_new(16);
		status->replacement_mapping=_vector_new(16, sizeof(int32_t));
		const char* current=replacement;
		const char* last_current=replacement;
		while(*current!='\0')
		{
			if(*current=='$' && *(current+1)>='1' && *(current+1)<='9')
			{
				if(current!=replacement && *(current-1)=='\\')
				{
					vector_push(status->between_replacements, string_new_from_to(last_current, 0, current-last_current-1));
					int32_t id=-1;
					_vector_push(status->replacement_mapping, &id);
					last_current=current;
				}
				else
				{
					vector_push(status->between_replacements, string_new_from_to(last_current, 0, current-last_current));
					last_current=current+2;
					while(*last_current>='1' && *(last_current)<='9')last_current++;
					string_t* group_id=string_new_from_to(current, 1, last_current-current);
					int32_t id=string_to_int32(group_id);
					if(id<1 || id> status->num_subs)
						warn("invalid group replacement \"$%s\", s/%s/%s/%s", group_id->data, pattern, replacement, flags_string);
					_vector_push(status->replacement_mapping, &id);
					string_free(group_id);
					current=last_current-1;
				}
			}
			current++;
		}
		vector_push(status->between_replacements, string_new_from_to(last_current, 0, current-last_current));
		vector_optimize(status->between_replacements);
		vector_optimize(status->replacement_mapping);
	}
	return status;
}

vector_t* string_match(string_t* input, const char* pattern, const char* flags)
{
	regexstatus_t* status=_regexstatus_new(pattern, NULL, flags);
	int regex_flags=0;
	if(status->end!=0)regex_flags|=PCRE_NOTBOL;
	if((status->flags & REGEX_FLAG_CONTINUE) == 0)
	{
		status->start=0;
		status->end=0;
	}
	int ovector[status->num_subs*3];
	int result=pcre_exec(status->expression,NULL,input->data, input->length, status->start, regex_flags, ovector, status->num_subs*3);
	vector_t* groups=NULL;
	if(((result>=0 && (status->flags & REGEX_FLAG_REVERSE)==0)) || (result<0 && (status->flags & REGEX_FLAG_REVERSE)!=0))
	{
		if(status->flags & REGEX_FLAG_NOSUB)
		{
			groups=(vector_t*)1;
		}
		else
		{
			groups=vector_new(status->num_subs);
			groups->length=status->num_subs;
			size_t i=0;
			for(i=0;i<status->num_subs;i++)
			{
				vector_set(groups,i,string_new_from_to(input->data,ovector[i*2],ovector[i*2+1]));
			}
			status->start=ovector[1];
			status->end=input->length;
		}
	}
#ifdef STRING_REGEX_USE_CACHE
	if(status->flags & REGEX_FLAG_ONCE)
#endif
		_regexstatus_free(status);
	return groups;
}

array_t* string_split(string_t* input, const char* separator, const char* flags)
{
	array_t* output=array_new();
	regexstatus_t* status=_regexstatus_new(separator, NULL, flags);
	int last_start=0;
	int ovector[3];
	while(pcre_exec(status->expression,NULL,input->data,input->length,last_start, 0, ovector, 3)>=0)
	{
		if(ovector[0]!=0)array_push(output,string_new_from_to(input->data,last_start,ovector[0]));
		last_start=ovector[1];
	}
	if(last_start!=input->length)array_push(output,string_new_from_to(input->data,last_start,input->length));
#ifdef STRING_REGEX_USE_CACHE
	if(status->flags & REGEX_FLAG_ONCE)
#endif
		_regexstatus_free(status);
	return output;
}

array_t* string_array_grep(array_t* input, const char* pattern, const char* flags)
{
	array_t* output=NULL;
	regexstatus_t* status=_regexstatus_new(pattern, NULL, flags);
	int i=0;
	for(i=0;i<input->length;i++)
	{
		string_t* string=array_get(input,i);
		int result=pcre_exec(status->expression,NULL,string->data,string->length,0,0,NULL,0);
		if(string!=NULL && ((result<0 && (status->flags & REGEX_FLAG_REVERSE)==0) || (result>=0 && (status->flags & REGEX_FLAG_REVERSE)!=0)))
		{
			if(output==NULL)output=array_new();
			array_push(output, string_copy(string));
		}
	}
#ifdef STRING_REGEX_USE_CACHE
	if(status->flags & REGEX_FLAG_ONCE)
#endif
		_regexstatus_free(status);
	return output;
}

int string_replace(string_t* input, const char* pattern, const char* replacement, const char* flags)
{
	regexstatus_t* status=_regexstatus_new(pattern, replacement, flags);
	int last_start=0;
	int i;
	int matched=0;
	array_t* output=array_new();
	int ovector[status->num_subs*3];
	while(pcre_exec(status->expression,NULL,input->data,input->length,last_start,0,ovector,status->num_subs*3)>=0)
	{
		if(ovector[0]>0)array_push(output,string_new_from_to(input->data,last_start,ovector[0]));
		for(i=0; i<status->replacement_mapping->length; i++)
		{
			array_push(output,string_copy(vector_get(status->between_replacements, i)));
			int32_t* id=_vector_get(status->replacement_mapping, i);
			if(*id>0 && *id<status->num_subs)
				array_push(output,string_new_from_to(input->data, ovector[(*id)*2], ovector[(*id)*2+1]));
		}
		array_push(output,string_copy(vector_get(status->between_replacements, status->between_replacements->length-1)));
		last_start=ovector[1];
		matched=1;
		if((status->flags & REGEX_FLAG_GLOBAL) == 0)break;
	}
	if(matched)
	{
		if(last_start!=input->length)array_push(output,string_new_from_to(input->data,last_start,input->length));
		string_t* joint_output=string_join_cstr("",output);
		FREE(input->data);
		input->data=joint_output->data;
		input->length=joint_output->length;
		input->size=joint_output->size;
		FREE(joint_output);
	}
	string_array_free(output);
#ifdef STRING_REGEX_USE_CACHE
	if(status->flags & REGEX_FLAG_ONCE)
#endif
		_regexstatus_free(status);
	return matched;
}

#else // posix regex

typedef struct regexstatus {
	regex_t expression;
	//vector_t* groups;
	size_t start;
	size_t end;
	int flags;
	const char* replacement;
	vector_t* between_replacements;
	vector_t* replacement_mapping;
} regexstatus_t;

#define REGEX_FLAG_NONE 0
#define REGEX_FLAG_GLOBAL (1<<0)
#define REGEX_FLAG_CONTINUE (1<<1)
#define REGEX_FLAG_CASE_INSENSITIVE (1<<2)
#define REGEX_FLAG_MULTILINE (1<<3)
#define REGEX_FLAG_ONCE (1<<4)
#define REGEX_FLAG_NOSUB (1<<5)
#define REGEX_FLAG_REVERSE (1<<6)

void _regexstatus_free(regexstatus_t* status)
{
	if(status->replacement_mapping!=NULL)vector_free(status->replacement_mapping);
	if(status->between_replacements!=NULL)string_vector_free(status->between_replacements);
	regfree(&status->expression);
	FREE(status);
}

#ifdef STRING_REGEX_USE_CACHE
hashtable_t* _regex_cache=NULL;
#endif

regexstatus_t* _regexstatus_new(const char* pattern, const char* replacement, const char* flags_string)
{
	regexstatus_t* status=NULL;
	int flags=REGEX_FLAG_NONE;
	if(flags_string!=NULL)
	{
		const char* current_flag=flags_string;
		while(current_flag && *current_flag!='\0')
		{
			switch(*current_flag) {
				case 'g': flags|=REGEX_FLAG_GLOBAL;break;
				case 'c': flags|=REGEX_FLAG_CONTINUE;break;
				case 'i': flags|=REGEX_FLAG_CASE_INSENSITIVE;break;
				case 'm': flags|=REGEX_FLAG_MULTILINE;break;
				case 'o': flags|=REGEX_FLAG_ONCE;break;
				case 'n': flags|=REGEX_FLAG_NOSUB;break;
				case '!': flags|=REGEX_FLAG_REVERSE;break;
				default:
					if(replacement==NULL) {
						die("invlid flag \"%c\", in m/%s/%s",*current_flag, pattern, flags_string);
					} else {
						die("invlid flag \"%c\", in s/%s/%s/%s",*current_flag, pattern, replacement, flags_string);
					}
					break;
			}
			current_flag++;
		}
	}
#ifdef STRING_REGEX_USE_CACHE
	if((flags & REGEX_FLAG_ONCE) == 0)
	{
		if(_regex_cache==NULL)
		{
			_regex_cache=hashtable_new();
		}
		else
		{
			status=hashtable_get(_regex_cache, pattern, strlen(pattern));
		}
	}
#endif
	if(status==NULL || ((status->flags & (REGEX_FLAG_CASE_INSENSITIVE|REGEX_FLAG_MULTILINE|REGEX_FLAG_NOSUB)) != 
			(flags & (REGEX_FLAG_CASE_INSENSITIVE|REGEX_FLAG_MULTILINE|REGEX_FLAG_NOSUB))))
	{
		if(status==NULL)
		{
			status=MALLOC(sizeof(regexstatus_t));
			memset(status,0,sizeof(regexstatus_t));
#ifdef STRING_REGEX_USE_CACHE
			if((flags & REGEX_FLAG_ONCE) == 0)hashtable_set(_regex_cache, pattern, strlen(pattern), status);
#endif
		}
		else
		{
			regfree(&status->expression);
		}
		status->flags=flags;
		int regcomp_result=regcomp(&status->expression, pattern, 
			(flags & REGEX_FLAG_CASE_INSENSITIVE ? REG_ICASE : 0) |
			(flags & REGEX_FLAG_MULTILINE ? REG_NEWLINE : 0) |
			(flags & REGEX_FLAG_NOSUB ? REG_NOSUB : 0) | 
			REG_EXTENDED);
		if(regcomp_result!=0)
		{
			char buffer[1024];
			regerror(regcomp_result, &status->expression, buffer, 1024);
			if(replacement==NULL)
			{ 
				die("compiling regex m/%s/%s, %s", pattern, flags_string, buffer);
			} else {
				die("compiling regex s/%s/%s/%s, %s", pattern, replacement, flags_string, buffer);
			}
		}
		status->expression.re_nsub++;
	}
	if(replacement!=NULL && (status->replacement==NULL || strcmp(status->replacement,replacement)!=0))
	{
		status->replacement=replacement;
		status->between_replacements=vector_new(16);
		status->replacement_mapping=_vector_new(16, sizeof(int32_t));
		const char* current=replacement;
		const char* last_current=replacement;
		while(*current!='\0')
		{
			if(*current=='$' && *(current+1)>='1' && *(current+1)<='9')
			{
				if(current!=replacement && *(current-1)=='\\')
				{
					vector_push(status->between_replacements, string_new_from_to(last_current, 0, current-last_current-1));
					int32_t id=-1;
					_vector_push(status->replacement_mapping, &id);
					last_current=current;
				}
				else
				{
					vector_push(status->between_replacements, string_new_from_to(last_current, 0, current-last_current));
					last_current=current+2;
					while(*last_current>='1' && *(last_current)<='9')last_current++;
					string_t* group_id=string_new_from_to(current, 1, last_current-current);
					int32_t id=string_to_int32(group_id);
					if(id<1 || id>status->expression.re_nsub)warn("invalid group replacement \"$%s\", s/%s/%s/%s, %ld group%s", group_id->data, 
						pattern, replacement, flags_string, status->expression.re_nsub-1,status->expression.re_nsub-1>1?"s":"");
					_vector_push(status->replacement_mapping, &id);
					string_free(group_id);
					current=last_current-1;
				}
			}
			current++;
		}
		vector_push(status->between_replacements, string_new_from_to(last_current, 0, current-last_current));
		vector_optimize(status->between_replacements);
		vector_optimize(status->replacement_mapping);
	}
	return status;
}

vector_t* string_match(string_t* input, const char* pattern, const char* flags)
{
	regexstatus_t* status=_regexstatus_new(pattern, NULL, flags);
	int regex_flags=0;
	if(status->end!=0)regex_flags|=REG_NOTBOL;
	regex_flags|=REG_STARTEND;
	if((status->flags & REGEX_FLAG_CONTINUE) == 0)
	{
		status->start=0;
		status->end=0;
	}
	regmatch_t match[status->expression.re_nsub];
	match[0].rm_so=status->end;
	match[0].rm_eo=input->length;
	int result=regexec(&status->expression,input->data,status->expression.re_nsub,match,regex_flags);
	vector_t* groups=NULL;
	if(((result==0 && (status->flags & REGEX_FLAG_REVERSE)==0)) || (result!=0 && (status->flags & REGEX_FLAG_REVERSE)!=0))
	{
		if(status->flags & REGEX_FLAG_NOSUB)
		{
			groups=(vector_t*)1;
		}
		else
		{
			groups=vector_new(status->expression.re_nsub);
			groups->length=status->expression.re_nsub;
			size_t i=0;
			for(i=0;i<status->expression.re_nsub;i++)
			{
				vector_set(groups,i,string_new_from_to(input->data,match[i].rm_so,match[i].rm_eo));
			}
			status->start=match[0].rm_so;
			status->end=match[0].rm_eo;
		}
	}
#ifdef STRING_REGEX_USE_CACHE
	if(status->flags & REGEX_FLAG_ONCE)
#endif
		_regexstatus_free(status);
	return groups;
}

array_t* string_split(string_t* input, const char* separator, const char* flags)
{
	array_t* output=array_new();
	regexstatus_t* status=_regexstatus_new(separator, NULL, flags);
	regmatch_t match[status->expression.re_nsub];
	match[0].rm_so=0;
	match[0].rm_eo=input->length;
	regoff_t last_start=0;
	while(regexec(&status->expression,input->data,status->expression.re_nsub,match,REG_STARTEND)==0)
	{
		if(match[0].rm_so!=0)array_push(output,string_new_from_to(input->data,last_start,match[0].rm_so));
		last_start=match[0].rm_eo;
		match[0].rm_so=match[0].rm_eo;
		match[0].rm_eo=input->length;
	}
	if(last_start!=input->length)array_push(output,string_new_from_to(input->data,last_start,input->length));
#ifdef STRING_REGEX_USE_CACHE
	if(status->flags & REGEX_FLAG_ONCE)
#endif
		_regexstatus_free(status);
	return output;
}

array_t* string_array_grep(array_t* input, const char* pattern, const char* flags)
{
	array_t* output=NULL;
	regexstatus_t* status=_regexstatus_new(pattern, NULL, flags);
	int i=0;
	for(i=0;i<input->length;i++)
	{
		string_t* string=array_get(input,i);
		int result=regexec(&status->expression,string->data,0,NULL,0);
		if(string!=NULL && ((result==0 && (status->flags & REGEX_FLAG_REVERSE)==0) || (result!=0 && (status->flags & REGEX_FLAG_REVERSE)!=0)))
		{
			if(output==NULL)output=array_new();
			array_push(output, string_copy(string));
		}
	}
#ifdef STRING_REGEX_USE_CACHE
	if(status->flags & REGEX_FLAG_ONCE)
#endif
		_regexstatus_free(status);
	return output;
}

int string_replace(string_t* input, const char* pattern, const char* replacement, const char* flags)
{
	regexstatus_t* status=_regexstatus_new(pattern, replacement, flags);
	regmatch_t match[status->expression.re_nsub];
	match[0].rm_so=0;
	match[0].rm_eo=input->length;
	regoff_t last_start=0;
	int i;
	int matched=0;
	array_t* output=array_new();
	while(regexec(&status->expression,input->data,status->expression.re_nsub,match,REG_STARTEND)==0)
	{
		if(match[0].rm_so!=0)array_push(output,string_new_from_to(input->data,last_start,match[0].rm_so));
		for(i=0; i<status->replacement_mapping->length; i++)
		{
			array_push(output,string_copy(vector_get(status->between_replacements, i)));
			int32_t* id=_vector_get(status->replacement_mapping, i);
			if(*id>0 && *id<status->expression.re_nsub)
				array_push(output,string_new_from_to(input->data, match[*id].rm_so, match[*id].rm_eo));
		}
		array_push(output,string_copy(vector_get(status->between_replacements, status->between_replacements->length-1)));
		last_start=match[0].rm_eo;
		match[0].rm_so=match[0].rm_eo;
		match[0].rm_eo=input->length;
		matched=1;
		if((status->flags & REGEX_FLAG_GLOBAL) == 0)break;
	}
	if(matched)
	{
		if(last_start!=input->length)array_push(output,string_new_from_to(input->data,last_start,input->length));
		string_t* joint_output=string_join_cstr("",output);
		FREE(input->data);
		input->data=joint_output->data;
		input->length=joint_output->length;
		input->size=joint_output->size;
		FREE(joint_output);
	}
	string_array_free(output);
#ifdef STRING_REGEX_USE_CACHE
	if(status->flags & REGEX_FLAG_ONCE)
#endif
		_regexstatus_free(status);
	return matched;
}

#endif // USE_PCRE

void string_array_free(array_t* input)
{
	size_t i;
	for(i=0;i<input->length;i++)
	{
		string_free((string_t*)array_get(input,i));
	}
	array_free(input);
}

void string_vector_free(vector_t* input)
{
	size_t i;
	for(i=0;i<input->length;i++)
	{
		string_free((string_t*)vector_get(input,i));
	}
	vector_free(input);
}

int32_t string_to_int32(string_t* input)
{
	char* error=NULL;
	int32_t output=strtol(input->data,&error,10);
	if(error!=NULL && *error=='\0')return output;
	warn("string_to_int32(%s) failed",input->data);
	return 0;
}

int64_t string_to_int64(string_t* input)
{
	char* error=NULL;
	int64_t output=strtoll(input->data,&error,10);
	if(error!=NULL && *error=='\0')return output;
	warn("string_to_int64(%s) failed",input->data);
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

string_t* string_readline(FILE* file)
{
	string_t* output=NULL;
	char buffer[1024];
	do
	{
		char* result=fgets(buffer,1024,file);
		if(result==NULL)return output;
		if(output==NULL) output=string_new(buffer);
		else string_append_cstr(output,buffer);
	}
	while(strlen(buffer)==1023 && buffer[1022]!='\n');
	return output;
}
