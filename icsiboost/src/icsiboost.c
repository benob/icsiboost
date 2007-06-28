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

#if HAVE_CONFIG_H
# include <config.h>
#endif

#define _ISOC99_SOURCE // to get the NAN symbol
//#define USE_THREADS

#include "utils/utils.h"
#include "utils/vector.h"
#include "utils/string.h"
#include "utils/hashtable.h"
#include "utils/mapped.h"
#include "utils/array.h"

#ifdef USE_THREADS
#ifndef _REENTRANT
#define _REENTRANT
#endif
#include <semaphore.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/*
WARNING:
- the vector code is not 64bit safe. will be reworked.
- garbage collection is not working yet => do not activate it
- threads have a strange behaviour on osx
TODO:
- loading/saving a model
- test mode
- fixed load hash tables (adapt number of buckets)
- mutex/condition based semaphores
- free memory and remove leaks (and go through that again and again)
- bag-of-ngrams features
- frequency cutoff
- sampleing and ranking
- more COMMENTS !!!
*/
#define FEATURE_TYPE_IGNORE 0
#define FEATURE_TYPE_CONTINUOUS 1
#define FEATURE_TYPE_TEXT 2
#define FEATURE_TYPE_SCORED_TEXT 3
#define FEATURE_TYPE_SET 4

typedef struct template { // a column definition
	int column;
	string_t* name;
	int type;
	hashtable_t* dictionary;       // dictionary for token based template
	vector_t* tokens;              // the actual tokens
	//vector_t* dictionary_counts;
	vector_t* ordered;             // ordered example ids according to value for continuous columns
} template_t;

typedef struct example { // an instance
	vector_t* features;            // vector of (int or float according to templates)
	int class;
	double* weight;                // example weight by class
	double* score;                 // example score by class, updated iteratively
} example_t;

#define CLASSIFIER_TYPE_THRESHOLD 1
#define CLASSIFIER_TYPE_TEXT 2

typedef struct weakclassifier { // a weak learner
	template_t* template;          // the corresponding template
	int type;                      // type in CLASSIFIER_TYPE_TEXT or CLASSIFIER_TYPE_THRESHOLD
	int32_t token;                     // token for TEXT classifier
	int column;                    // redundant with template
	double threshold;              // threshold for THRESHOLD classifier
	double alpha;                  // alpha from the paper
	double objective;              // Z() value from minimization
	double *c0;                    // weight by class, unknown
	double *c1;                    // weight by class, token absent or below threshold
	double *c2;                    // weight by class, token present or above threshold
} weakclassifier_t;

typedef struct tokeninfo { // store info about a word (or token)
	int32_t id;
	char* key;
	size_t count;
} tokeninfo_t;

double smoothing=0.5;              // -E <number> in boostexter
int verbose=0;

#define y_l(x,y) (x->class==y?1.0:-1.0)    // y_l() from the paper
#define b(x,y) (x->class==y?1:0)           // b() from the paper (binary class match)

weakclassifier_t* train_text_stump(double min_objective, template_t* template, vector_t* examples, double** sum_of_weights, int num_classes)
{
	int i,l;
	int column=template->column;
	size_t num_tokens=template->tokens->length;
	int32_t t;

	double* weight[2][num_classes]; // weight[b][label][token] (=> D() in the paper) , only for token presence (absence is infered from sum_of_weights)
	for(l=0;l<num_classes;l++)
	{
		weight[0][l]=MALLOC(sizeof(double)*num_tokens);
		weight[1][l]=MALLOC(sizeof(double)*num_tokens);
	}
	for(t=1;t<num_tokens;t++)  // initialize
	{
		for(l=0;l<num_classes;l++)
		{
			weight[0][l][t]=0.0;
			weight[1][l][t]=0.0;
		}
	}
	for(i=0;i<examples->length;i++) // compute the presence weights
	{
		example_t* example=(example_t*)vector_get(examples,i);
		int32_t token=vector_get_int32(example->features,column);
		for(l=0;l<num_classes;l++)
		{
			weight[b(example,l)][l][token]+=example->weight[l];
		}
	}
	weakclassifier_t* classifier=NULL; // init an empty classifier
	classifier=MALLOC(sizeof(weakclassifier_t));
	classifier->template=template;
	classifier->threshold=NAN;
	classifier->alpha=1.0;
	classifier->type=CLASSIFIER_TYPE_TEXT;
	classifier->token=0;
	classifier->column=column;
	classifier->objective=1.0;
	classifier->c0=MALLOC(sizeof(double)*num_classes);
	classifier->c1=MALLOC(sizeof(double)*num_classes);
	classifier->c2=MALLOC(sizeof(double)*num_classes);
	double epsilon=smoothing/(num_classes*examples->length);

	for(t=1;t<num_tokens;t++)
	{
		double objective=0;
		for(l=0;l<num_classes;l++) // compute the objective function Z()=sum_j(sum_l(sqrt(W+*W-))
		{
			if(weight[0][l][t]<0)weight[0][l][t]=0.0;
			if(weight[1][l][t]<0)weight[1][l][t]=0.0;
			objective+=sqrt((sum_of_weights[1][l]-weight[1][l][t])*(sum_of_weights[0][l]-weight[0][l][t]));
			objective+=sqrt(weight[1][l][t]*weight[0][l][t]);
		}
		objective*=2;
		fprintf(stdout,"DEBUG: column=%d token=%d obj=%f\n",column,t,objective);
		if(objective-min_objective<-1e-11) // select the argmin()
		{
			min_objective=objective;
			classifier->token=t;
			classifier->objective=objective;
			for(l=0;l<num_classes;l++)  // update c0, c1 and c2 => c0 and c1 are the same for text stumps
			{
				classifier->c0[l]=0.5*log((sum_of_weights[1][l]-weight[1][l][t]+epsilon)/(sum_of_weights[0][l]-weight[0][l][t]+epsilon));
				classifier->c1[l]=classifier->c0[l];
				//classifier->c0[l]=0;
				//classifier->c1[l]=0.5*log((sum_of_weights[1][l]-weight[1][l][t]+epsilon)/(sum_of_weights[0][l]-weight[0][l][t]+epsilon));
				classifier->c2[l]=0.5*log((weight[1][l][t]+epsilon)/(weight[0][l][t]+epsilon));
			}
		}
	}
	for(l=0;l<num_classes;l++) // free memory
	{
		free(weight[0][l]);
		free(weight[1][l]);
	}
	if(classifier->token==0) // no better classifier has been found
	{
		FREE(classifier->c0);
		FREE(classifier->c1);
		FREE(classifier->c2);
		FREE(classifier);
		return NULL;
	}
	return classifier;
}

weakclassifier_t* train_continuous_stump(double min_objective, template_t* template, vector_t* examples, int num_classes)
{
	size_t i,j,l;
	int column=template->column;
	vector_t* ordered=template->ordered;
	if(ordered==NULL) // only order examples once, then keep the result in the template
	{
		ordered=vector_new_type(examples->length,sizeof(int32_t));
		ordered->length=examples->length;
		int32_t index=0;
		for(index=0;index<examples->length;index++)vector_set_int32(ordered,index,(int32_t)index);
		for(i=0;i<examples->length && i<4;i++)fprintf(stdout,"%d %f\n",i,vector_get_float(((example_t*)vector_get(examples,i))->features,column));
		ordered->length=examples->length;
		int local_comparator(const void* a, const void* b)
		{
			int32_t aa=*(int32_t*)a;
			int32_t bb=*(int32_t*)b;
			float aa_value=vector_get_float(((example_t*)vector_get(examples,(size_t)aa))->features,column);
			float bb_value=vector_get_float(((example_t*)vector_get(examples,(size_t)bb))->features,column);
			//fprintf(stdout,"%d(%f) <=> %d(%f)\n",aa,aa_value,bb,bb_value);
			if(aa_value<bb_value)return -1;
			if(aa_value>bb_value)return -1;
			//if(isnan(aa_value) || aa_value>bb_value)return 1; // put the NAN (unknown values) at the end of the list
			//if(isnan(bb_value) || aa_value<bb_value)return -1;
			return 0;
		}
		vector_sort(ordered,local_comparator);
		template->ordered=ordered;
	}

	double weight[3][2][num_classes]; // D(j,b,l)
	for(j=0;j<3;j++)
		for(l=0;l<num_classes;l++)
		{
			weight[j][0][l]=0.0;
			weight[j][1][l]=0.0;
		}
	for(i=0;i<ordered->length;i++) // compute the "unknown" weights and the weight of examples after threshold
	{
		example_t* example=vector_get(examples,vector_get_int32(ordered,i));
		//fprintf(stderr,"%d %f\n",column,vector_get_float(example->features,column));
		for(l=0;l<num_classes;l++)
		{
			if(isnan(vector_get_float(example->features,column)))
				weight[0][b(example,l)][l]+=example->weight[l];
			else
				weight[2][b(example,l)][l]+=example->weight[l];
		}
	}
	weakclassifier_t* classifier=NULL; // new classifier
	classifier=MALLOC(sizeof(weakclassifier_t));
	classifier->template=template;
	classifier->threshold=NAN;
	classifier->alpha=1.0;
	classifier->type=CLASSIFIER_TYPE_THRESHOLD;
	classifier->token=0;
	classifier->column=column;
	classifier->objective=1.0;
	classifier->c0=MALLOC(sizeof(double)*num_classes);
	classifier->c1=MALLOC(sizeof(double)*num_classes);
	classifier->c2=MALLOC(sizeof(double)*num_classes);
	double epsilon=smoothing/(num_classes*examples->length);

	for(i=0;i<ordered->length-1;i++) // compute the objective function at every possible threshold (in between examples)
	{
		example_t* example=(example_t*)vector_get(examples,(size_t)vector_get_int32(ordered,i));
		fprintf(stdout,"%zd %zd %f\n",i,vector_get_int32(ordered,i),vector_get_float(example->features,column));
		if(isnan(vector_get_float(example->features,column)))continue; // skip unknown values
		example_t* next_example=(example_t*)vector_get(examples,(size_t)vector_get_int32(ordered,i+1));
		for(l=0;l<num_classes;l++) // update the objective function by putting the current example the other side of the threshold
		{
			weight[1][b(example,l)][l]+=example->weight[l];
			weight[2][b(example,l)][l]-=example->weight[l];
		}
		if(vector_get(example->features,column)==vector_get(next_example->features,column))continue; // same value
		double objective=0;
		for(l=0;l<num_classes;l++) // compute objective Z()
		{
			if(weight[0][1][l]<0)weight[0][1][l]=0.0;
			if(weight[0][0][l]<0)weight[0][0][l]=0.0;
			if(weight[1][1][l]<0)weight[1][1][l]=0.0;
			if(weight[1][0][l]<0)weight[1][0][l]=0.0;
			if(weight[2][1][l]<0)weight[2][1][l]=0.0;
			if(weight[2][0][l]<0)weight[2][0][l]=0.0;
			objective+=sqrt(weight[0][1][l]*weight[0][0][l]);
			objective+=sqrt(weight[1][1][l]*weight[1][0][l]);
			objective+=sqrt(weight[2][1][l]*weight[2][0][l]);
		}
		objective*=2;
		fprintf(stdout,"DEBUG: column=%d threshold=%f obj=%f\n",column,(vector_get_float(next_example->features,column)+vector_get_float(example->features,column))/2,objective);
		if(objective-min_objective<-1e-11) // get argmin
		{
			classifier->objective=objective;
			classifier->threshold=(vector_get_float(next_example->features,column)+vector_get_float(example->features,column))/2; // threshold between current and next example
			if(isnan(classifier->threshold))die("threshold is nan, column=%d, objective=%f, i=%d",column,objective,i); // should not happend
			//fprintf(stdout," %d:%d:%f",column,i,classifier->threshold);
			min_objective=objective;
			for(l=0;l<num_classes;l++) // update class weight
			{
				classifier->c0[l]=0.5*log((weight[0][1][l]+epsilon)/(weight[0][0][l]+epsilon));
				classifier->c1[l]=0.5*log((weight[1][1][l]+epsilon)/(weight[1][0][l]+epsilon));
				classifier->c2[l]=0.5*log((weight[2][1][l]+epsilon)/(weight[2][0][l]+epsilon));
			}
		}
	}
	if(isnan(classifier->threshold)) // not found a better classifier
	{
		FREE(classifier->c0);
		FREE(classifier->c1);
		FREE(classifier->c2);
		FREE(classifier);
		return NULL;
	}
	return classifier;
}

#ifdef USE_THREADS
#define SHARED(type,name,value) pthread_mutex_t name ## _mutex = PTHREAD_MUTEX_INITIALIZER; type name=value;
#define SHARED_NOINIT(type,name) pthread_mutex_t name ## _mutex; type name;
#define LOCK(variable) pthread_mutex_lock(&(variable ## _mutex));
#define UNLOCK(variable) pthread_mutex_unlock(&(variable ## _mutex));
SHARED(int,finished,0);

typedef struct workertoolbox {
	vector_t* examples; // stuff that matter to the weak learner
	vector_t* templates;
	vector_t* classes;
	double **sum_of_weights;
	SHARED_NOINIT(int,next_column); // the next column to process
	SHARED_NOINIT(weakclassifier_t*,best_classifier); // the result of a job
	sem_t* ready_to_process; // if available, a thread can start working
	sem_t* result_available; // if available, a result is avaiable in best_classifier
} workertoolbox_t;


workertoolbox_t* toolbox=NULL;

void* threaded_worker(void* data)
{
	int column;
	int worker_num=*((int*)data);
	while(!finished)
	{
		sem_wait(toolbox->ready_to_process);
		if(finished)pthread_exit(0);
		LOCK(toolbox->next_column);
		column=toolbox->next_column;
		toolbox->next_column++;
		UNLOCK(toolbox->next_column);
		template_t* template=(template_t*)vector_get(toolbox->templates,column);
		if(verbose)fprintf(stdout,"%d worker thread processing: %s\n",worker_num,template->name->data);
		//----------- do the job
		weakclassifier_t* current=NULL;
		if(template->type==FEATURE_TYPE_CONTINUOUS)
		{
			current=train_continuous_stump(1.0, template, toolbox->examples, toolbox->classes->length);
		}
		else if(template->type==FEATURE_TYPE_TEXT)
		{
			current=train_text_stump(1.0, template, toolbox->examples, toolbox->sum_of_weights, toolbox->classes->length);
		}
		//----------- return the result
		if(current!=NULL)
		{
			//fprintf(stdout,"obj:%f %f\n",current->objective,toolbox->best_classifier->objective);
			LOCK(toolbox->best_classifier);
			if(current->objective-toolbox->best_classifier->objective<-1e-11)
			{
				toolbox->best_classifier->template=current->template;
				toolbox->best_classifier->type=current->type;
				toolbox->best_classifier->token=current->token;
				toolbox->best_classifier->column=current->column;
				toolbox->best_classifier->threshold=current->threshold;
				toolbox->best_classifier->alpha=current->alpha;
				toolbox->best_classifier->objective=current->objective;
				memcpy(toolbox->best_classifier->c0,current->c0,sizeof(double)*toolbox->classes->length);
				memcpy(toolbox->best_classifier->c1,current->c1,sizeof(double)*toolbox->classes->length);
				memcpy(toolbox->best_classifier->c2,current->c2,sizeof(double)*toolbox->classes->length);
			}
			UNLOCK(toolbox->best_classifier);
			FREE(current->c0);
			FREE(current->c1);
			FREE(current->c2);
			FREE(current);
		}
		sem_post(toolbox->result_available);
	}
	pthread_exit(NULL);
}
#endif

/* compute error rate AND update weights
   in testing conditions (dev or test set) sum_of_weights is NULL, so just compute error rate
*/
double compute_classification_error(vector_t* classifiers, vector_t* examples, double** sum_of_weights, int num_classes)
{
	int i=0;
	int j=0;
	int l=0;
	double error=0;
	double normalization=0;
	for(i=0;i<examples->length;i++)
	{
		example_t* example=(example_t*)vector_get(examples,i);
		for(l=0;l<num_classes;l++)
		{
			//example->score[l]=0;
			//for(j=0;j<classifiers->length;j++)
			j=classifiers->length-1; // only update score with current classifier
			{
				weakclassifier_t* classifier=(weakclassifier_t*)vector_get(classifiers,j);
				if(classifier->type==CLASSIFIER_TYPE_THRESHOLD)
				{
					float value=vector_get_float(example->features,classifier->column);
					if(isnan(value)) // unknown value -- need to update weight?
					{
						example->score[l]+=classifier->alpha*classifier->c0[l];
						if(j==classifiers->length-1) // update example weight
							example->weight[l]=example->weight[l]*exp(-classifier->alpha*y_l(example,l)*classifier->c0[l]);
					}
					else if(value<classifier->threshold) // below threshold
					{
						example->score[l]+=classifier->alpha*classifier->c1[l];
						if(j==classifiers->length-1) // update example weight
							example->weight[l]=example->weight[l]*exp(-classifier->alpha*y_l(example,l)*classifier->c1[l]);
					}
					else  // above threshold
					{
						example->score[l]+=classifier->alpha*classifier->c2[l];
						if(j==classifiers->length-1) // update example weight
							example->weight[l]=example->weight[l]*exp(-classifier->alpha*y_l(example,l)*classifier->c2[l]);
					}
				}
				else if(classifier->type==CLASSIFIER_TYPE_TEXT)
				{
					int32_t token=vector_get_int32(example->features,classifier->column);
					if(token==0) // unknown value -- need to update weight?
					{
						example->score[l]+=classifier->alpha*classifier->c0[l];
						if(j==classifiers->length-1) // update example weight
							example->weight[l]=example->weight[l]*exp(-classifier->alpha*y_l(example,l)*classifier->c0[l]);
					}
					else if(token!=classifier->token) // token is absent
					{
						example->score[l]+=classifier->alpha*classifier->c1[l];
						if(j==classifiers->length-1)
							example->weight[l]=example->weight[l]*exp(-classifier->alpha*y_l(example,l)*classifier->c1[l]);
					}
					else // token is present
					{
						example->score[l]+=classifier->alpha*classifier->c2[l];
						if(j==classifiers->length-1)
							example->weight[l]=example->weight[l]*exp(-classifier->alpha*y_l(example,l)*classifier->c2[l]);
					}
				}
			}
			//if(example->weight[l]<1.0/examples->length*num_classes)example->weight[l]=1.0/examples->length*num_classes;
			normalization+=example->weight[l]; // update Z() normalization (not the same Z as in optimization)
		}
		double max=-1;
		int argmax=0;
		for(l=0;l<num_classes;l++) // selected class = class with highest score
		{
			if(example->score[l]>max)
			{
				max=example->score[l];
				argmax=l;
			}
		}
		if(!b(example,argmax))error++; // error if the class is not the real class
	}
	if(sum_of_weights!=NULL)
	{
		double min_weight=examples->length*num_classes;
		double max_weight=0;
		//normalization/=num_classes*examples->length;
		for(l=0;l<num_classes;l++) // update the sum of weights by class
		{
			sum_of_weights[0][l]=0.0;
			sum_of_weights[1][l]=0.0;
		}
		for(i=0;i<examples->length;i++) // normalize the weights and do some stats for debuging
		{
			example_t* example=(example_t*)vector_get(examples,i);
			//fprintf(stdout,"%d",i);
			for(l=0;l<num_classes;l++)
			{
				example->weight[l]/=normalization;
				if(example->weight[l]<0)die("ERROR: negative weight: %d %d %f",i,l,example->weight[l]);
				if(min_weight>example->weight[l]){min_weight=example->weight[l];}
				if(max_weight<example->weight[l]){max_weight=example->weight[l];}
				//fprintf(stdout," %f",example->weight[l]);
				sum_of_weights[b(example,l)][l]+=example->weight[l];
			}
			//fprintf(stdout,"\n");
		}
		//fprintf(stdout,"norm=%.12f min=%.12f max=%.12f\n",normalization,min_weight,max_weight);
	}
	return error/examples->length;
}

/* load a data file
  if it is a test or dev file (in_test=1) then do not update dictionaries
  note: real test files without a class in the end will fail (this is not classification mode)
*/
vector_t* load_examples(const char* filename, vector_t* templates, vector_t* classes, int feature_count_cutoff, int in_test)
{
	int i,j;
	mapped_t* input = mapped_load_readonly(filename);
	if(input == NULL)
	{
		warn("can't load \"%s\"", filename);
		return NULL;
	}
	vector_t* examples = vector_new(16);
	int line_num = 0;
	char* begining_of_line = (char*)input->data;
	char* end_of_file = (char*)input->data+input->length;
	while(begining_of_line < end_of_file)// && examples->length<10000)
	{
		line_num++;
		while(begining_of_line<end_of_file && *begining_of_line==' ') begining_of_line++;
		if(*begining_of_line=='|' || *begining_of_line=='\n') // skip comments and blank lines
		{
			while(begining_of_line<end_of_file && *begining_of_line!='\n') begining_of_line++;
			begining_of_line++;
			continue;
		}
		if(begining_of_line >= end_of_file) die("unexpected end of file, line %d in %s", line_num, filename);
		example_t* example = MALLOC(sizeof(example_t)); // that's one new example per line
		example->features = vector_new_type(templates->length,sizeof(int32_t)); // we will store 32bit ints and floats
		example->features->length=templates->length;
		example->weight = NULL;
		char* current = begining_of_line;
		int i;
		for(i = 0; i < templates->length; i++) // get one feature per column
		{
			while(current < end_of_file && *current == ' ') current++; // strip spaces at begining
			if(current >= end_of_file) die("unexpected end of file, line %d in %s", line_num, filename);
			char* token = current;
			size_t length = 0;
			while(current < end_of_file && *current != ',') // get up to coma
			{
				current++;
				length++;
			}
			if(current >= end_of_file) die("unexpected end of file, line %d in %s", line_num, filename);
			while(*(token+length-1) == ' ' && length > 0) length--; // strip spaces as end
			char field[length+1];
			memcpy(field, token, length);
			field[length] = '\0';
			template_t* template = (template_t*)vector_get(templates,i);
			if(template->type == FEATURE_TYPE_CONTINUOUS)
			{
				float value=NAN;// unknwon is represented by Not-A-Number (NAN)
				char* error_location=NULL;
				if(strcmp(field,"?")) // if not unknown value
				{
					value = strtof(field, &error_location);
					if(error_location==NULL || *error_location!='\0')
					   die("could not convert \"%s\" to a number, line %d, char %td in %s", field, line_num, token-begining_of_line+1, filename);
				}
				vector_set_float(example->features,i,value); }
			else if(template->type == FEATURE_TYPE_TEXT)
			{
				if(strcmp(field,"?")) // if not unknwon value
				{
					tokeninfo_t* tokeninfo = hashtable_get(template->dictionary, field, length);
					if(tokeninfo == NULL)
					{
						if(in_test)tokeninfo=vector_get(template->tokens,0); // default to the unknown token
						else // update the dictionary with the new token
						{
							tokeninfo = (tokeninfo_t*)MALLOC(sizeof(tokeninfo_t));
							tokeninfo->id = template->tokens->length;
							tokeninfo->key = strdup(field);
							tokeninfo->count=0;
							hashtable_set(template->dictionary, field, length, tokeninfo);
							vector_push(template->tokens, tokeninfo);
						}
					}
					tokeninfo->count++;
					vector_set_int32(example->features,i,tokeninfo->id);
				}
				else
				{
					vector_set_int32(example->features,i,0); // unknown token is 0 (aka NULL)
				}
			}
			else
			{
				vector_set_int32(example->features,i,0); // unsupported feature type: everything is unknown
			}
			current++;
		}
		//fprintf(stdout,"%d %p\n",examples->length,vector_get(example->features,0));
		char* class = current; // get class label
		while(current < end_of_file && *current != '\n') current++; // up to end of line
		if(current >= end_of_file) die("unexpected end of file, line %d in %s", line_num, filename);
		while(class < current && *class == ' ') class++; // strip spaces at the begining
		size_t length=0;
		while(class < current && *(class+length) != ' ' && *(class+length) != '.' && *(class+length) != '\n') length++; // strip "." and spaces at the end
		char class_token[length+1];
		memcpy(class_token,class,length); // copy as a cstring
		class_token[length]='\0';
		int id = 0;
		for(id = 0; id < classes->length; id++) // find class linearly as we usually have a few classes
		{
			if(!strncmp(((string_t*)vector_get(classes,id))->data, class, length)) break;
		}
		if(id == classes->length) die("unknown class \"%s\", line %d, char %td in %s", class_token, line_num, class-begining_of_line+1, filename);
		example->class = id;
		vector_push(examples, example); // store example
		begining_of_line = current+1;
	}
	vector_optimize(examples); // reduce memory consumption
	if(!in_test) 
	{
		for(i=0;i<templates->length;i++)
		{
			template_t* template=(template_t*)vector_get(templates,i);
			hashtable_optimize(template->dictionary);
			vector_optimize(template->tokens);
		}
		/*for(i=0; i<templates->length; i++)
		{
			template_t* template=(template_t*)vector_get(templates,i);
			for(j=0; j<template->dictionary_counts->length; j++)
			{
				if((int)vector_get(template->dictionary_counts,j)<feature_count_cutoff)
				{
					void* element=hashtable_remove(template->dictionary,vector_get(template->tokens,j),strlen(vector_get(template->tokens,j)));
					if(element!=NULL)die("YOUPI %s", (char*)vector_get(template->tokens,j));
				}
			}
		}*/
	}
	// initalize weights and score
	for(i=0;i<examples->length;i++)
	{
		example_t* example=(example_t*)vector_get(examples,i);
		//fprintf(stdout,"%d %d %p\n",i,(int)vector_get(example->features,0), example->weight);
		example->weight=(double*)MALLOC(classes->length*sizeof(double));
		example->score=(double*)MALLOC(classes->length*sizeof(double));
		for(j=0;j<classes->length;j++)
		{
			example->weight[j]=1.0/(classes->length*examples->length); // 1/(m*k)
			example->score[j]=0.0;
		}
	}
	if(verbose)fprintf(stdout,"EXAMPLES: %s %zd\n",filename, examples->length);
	mapped_free(input);
	return examples;
}

void usage(char* program_name)
{
	fprintf(stderr,"USAGE: %s [-n <iterations>|-E <smoothing>|-V|-j <threads>|-f <cutoff>] -S <stem>\n",program_name);
	exit(1);
}

int main(int argc, char** argv)
{
#ifdef USE_GC
	GC_INIT();
#endif
	int maximum_iterations=10;
	int feature_count_cutoff=0;
#ifdef USE_THREADS
	int number_of_workers=1;
#endif
	string_t* stem=NULL;
	array_t* args=string_argv_to_array(argc,argv);
	string_t* arg=NULL;
	while((arg=(string_t*)array_shift(args))!=NULL)
	{
		if(string_eq_cstr(arg,"-n"))
		{
			string_free(arg);
			arg=(string_t*)array_shift(args);
			if(arg==NULL)die("value needed for -n");
			maximum_iterations=string_to_int32(arg);
			if(maximum_iterations<=0)die("invalid value for -n [%s]",arg->data);
		}
		else if(string_eq_cstr(arg,"-f"))
		{
			die("feature count cutoff not supported yet");
			/* string_free(arg);
			arg=(string_t*)array_shift(args);
			if(arg==NULL)die("value needed for -f");
			feature_count_cutoff=string_to_int32(arg);
			if(feature_count_cutoff<=0)die("invalid value for -f [%s]",arg->data);*/
		}
		else if(string_eq_cstr(arg,"-E"))
		{
			string_free(arg);
			arg=(string_t*)array_shift(args);
			if(arg==NULL)die("value needed for -E");
			smoothing=string_to_double(arg);
			if(isnan(smoothing) || smoothing<=0)die("invalid value for -E [%s]",arg->data);
		}
		else if(string_eq_cstr(arg,"-j"))
		{
#ifdef USE_THREADS
			string_free(arg);
			arg=(string_t*)array_shift(args);
			if(arg==NULL)die("value needed for -j");
			number_of_workers=string_to_int32(arg);
			if(number_of_workers<=0)die("invalid value for -j [%s]",arg->data);
#else
			die("thread support has not been activated at compile time");
#endif
		}
		else if(string_eq_cstr(arg,"-V"))
		{
			verbose=1;
		}
		else if(string_eq_cstr(arg,"-S") && stem==NULL)
		{
			string_free(arg);
			arg=(string_t*)array_shift(args);
			if(arg==NULL)die("value needed for -S");
			stem=string_copy(arg);
		}
		else usage(argv[0]);
		string_free(arg);
	}
	array_free(args);
	if(stem==NULL)usage(argv[0]);

	int i;

	// data structures
	vector_t* templates = vector_new(16);
	vector_t* classes = NULL;

	// read names file
	string_t* names_filename = string_copy(stem);
	string_append_cstr(names_filename, ".names");
	mapped_t* input = mapped_load_readonly(names_filename->data);
	if(input == NULL) die("can't load \"%s\"", names_filename->data);
	string_t* line = NULL;
	int line_num = 0;
	while((line = mapped_readline(input)) != NULL) // should add some validity checking !!!
	{
		regexstatus_t* status=string_match(line,"^(\\|| *$)",0,NULL); // skip comments and blank lines
		if(status)
		{
			free(status);
			continue;
		}
		if(classes != NULL) // this line contains a template definition
		{
			array_t* parts = string_split("(^ +| *: *| *\\.$)", line);
			template_t* template = (template_t*)MALLOC(sizeof(template_t));
			template->column = line_num-1;
			template->name = (string_t*)array_get(parts, 0);
			string_t* type = (string_t*)array_get(parts, 1);
			template->dictionary = hashtable_new(16384);
			template->tokens = vector_new(16);
			//template->dictionary_counts = vector_new(16);
			template->ordered=NULL;
			tokeninfo_t* unknown_token=(tokeninfo_t*)MALLOC(sizeof(tokeninfo_t));
			unknown_token->id=0;
			unknown_token->key=strdup("?");
			unknown_token->count=0;
			vector_push(template->tokens,unknown_token);
			if(!strcmp(type->data, "continuous")) template->type = FEATURE_TYPE_CONTINUOUS;
			else if(!strcmp(type->data, "text")) template->type = FEATURE_TYPE_TEXT;
			else if(!strcmp(type->data, "scored text")) template->type = FEATURE_TYPE_IGNORE;
			else if(!strcmp(type->data, "ignore")) template->type = FEATURE_TYPE_IGNORE;
			else template->type = FEATURE_TYPE_TEXT;
			vector_push(templates, template);
			string_free(type);
			array_free(parts);
			if(verbose)fprintf(stdout,"TEMPLATE: %d %s %d\n",template->column,template->name->data,template->type);
		}
		else // first line contains the class definitions
		{
			array_t* parts = string_split("(^ +| *, *| *\\.$)", line);
			classes = vector_from_array(parts);
			array_free(parts);
			if(verbose)
			{
				fprintf(stdout,"CLASSES:");
				for(i=0;i<classes->length;i++)fprintf(stdout," %s",((string_t*)vector_get(classes,i))->data);
				fprintf(stdout,"\n");
			}
		}
		string_free(line);
		line_num++;
	}
	vector_optimize(templates);
	mapped_free(input);
	string_free(names_filename);

	// load a train, dev and test sets (if available)
	string_t* data_filename = string_copy(stem);
	string_append_cstr(data_filename, ".data");
	vector_t* examples = load_examples(data_filename->data, templates, classes, feature_count_cutoff, 0);
	string_free(data_filename);

	vector_t* dev_examples=NULL;
	vector_t* test_examples=NULL;

	data_filename = string_copy(stem);
	string_append_cstr(data_filename, ".dev");
	dev_examples = load_examples(data_filename->data, templates, classes, 0, 1);
	string_free(data_filename);

	data_filename = string_copy(stem);
	string_append_cstr(data_filename, ".test");
	test_examples = load_examples(data_filename->data, templates, classes, 0, 1);
	string_free(data_filename);

	// initialize the sum of weights by classes (to infer the other side of the partition in binary classifiers)
	// sum_of_weights[b][l]
	double **sum_of_weights=(double**)MALLOC(sizeof(double*)*2);
	sum_of_weights[0]=(double*)MALLOC(sizeof(double)*classes->length);
	sum_of_weights[1]=(double*)MALLOC(sizeof(double)*classes->length);
	for(i=0;i<classes->length;i++)
	{
		sum_of_weights[0][i]=0.0;
		sum_of_weights[1][i]=0.0;
	}

	int l;
	for(i=0;i<examples->length;i++)
	{
		example_t* example=(example_t*)vector_get(examples,i);
		for(l=0;l<classes->length;l++)
		{
			sum_of_weights[b(example,l)][l]+=example->weight[l];
		}
	}

#ifdef USE_THREADS
	pthread_t workers[number_of_workers];
	//workertoolbox_t* toolbox=MALLOC(sizeof(workertoolbox_t));
	toolbox=MALLOC(sizeof(workertoolbox_t));
	toolbox->examples=examples;
	toolbox->templates=templates;
	toolbox->classes=classes;
	toolbox->sum_of_weights=sum_of_weights;
	toolbox->next_column=0;
	pthread_mutex_init(&toolbox->next_column_mutex,NULL);
	pthread_mutex_init(&toolbox->best_classifier_mutex,NULL);
	toolbox->best_classifier=MALLOC(sizeof(weakclassifier_t));
	toolbox->best_classifier->c0=MALLOC(sizeof(double)*classes->length);
	toolbox->best_classifier->c1=MALLOC(sizeof(double)*classes->length);
	toolbox->best_classifier->c2=MALLOC(sizeof(double)*classes->length);
	toolbox->best_classifier->objective=1.0;
	//toolbox->ready_to_process=MALLOC(sizeof(sem_t));
	//sem_init(toolbox->ready_to_process,0,0);
	sem_unlink("ready_to_process");
	toolbox->ready_to_process=sem_open("ready_to_process",O_CREAT,0700,0);
	if(toolbox->ready_to_process==(sem_t*)SEM_FAILED)die("ready to process %d",SEM_FAILED);
	//toolbox->result_available=MALLOC(sizeof(sem_t));
	//sem_init(toolbox->result_available,0,0);
	sem_unlink("result_available");
	toolbox->result_available=sem_open("result_avaiable",O_CREAT,0700,0);
	if(toolbox->result_available==(sem_t*)SEM_FAILED)die("result_available");
	for(i=0; i<number_of_workers; i++)
	{
		pthread_attr_t attributes;
		pthread_attr_init(&attributes);
		pthread_attr_setdetachstate(&attributes,PTHREAD_CREATE_JOINABLE);
		pthread_create(&workers[i],&attributes,threaded_worker,&i);
		pthread_attr_destroy(&attributes);
	}
#endif
	int iteration=0;
	vector_t* classifiers=vector_new(maximum_iterations);
	double theorical_error=1.0;
	for(iteration=0;iteration<maximum_iterations;iteration++)
	{
#ifdef USE_THREADS
		LOCK(toolbox->next_column);
		toolbox->next_column=0;
		UNLOCK(toolbox->next_column);
		LOCK(toolbox->best_classifier);
		toolbox->best_classifier->objective=1.0;
		UNLOCK(toolbox->best_classifier);
#else
		double min_objective=1.0;
#endif
		weakclassifier_t* classifier=NULL;
#ifdef USE_THREADS
		for(i=0;i<templates->length;i++) // find the best classifier
		{
			sem_post(toolbox->ready_to_process);
		}
		// need to store and reorder potential classifiers to get a deterministic behaviour
		for(i=0;i<templates->length;i++) // wait for the results
		{
			sem_wait(toolbox->result_available);
		}
		// all results should be available
		classifier=MALLOC(sizeof(weakclassifier_t));
		memcpy(classifier,toolbox->best_classifier,sizeof(weakclassifier_t));
		classifier->c0=MALLOC(sizeof(double)*classes->length);
		classifier->c1=MALLOC(sizeof(double)*classes->length);
		classifier->c2=MALLOC(sizeof(double)*classes->length);
		memcpy(classifier->c0,toolbox->best_classifier->c0,sizeof(double)*classes->length);
		memcpy(classifier->c1,toolbox->best_classifier->c1,sizeof(double)*classes->length);
		memcpy(classifier->c2,toolbox->best_classifier->c2,sizeof(double)*classes->length);
#else
		for(i=0;i<templates->length;i++) // fine the best classifier
		{
			template_t* template=(template_t*)vector_get(templates,i);
			weakclassifier_t* current=NULL;
			if(template->type==FEATURE_TYPE_CONTINUOUS)
			{
				current=train_continuous_stump(min_objective, template, examples, classes->length);
			}
			else if(template->type==FEATURE_TYPE_TEXT)
			{
				current=train_text_stump(min_objective, template, examples, sum_of_weights, classes->length);
			}
			if(current==NULL)continue;
			if(current->objective-min_objective<-1e-11)
			{
				min_objective=current->objective;
				if(classifier!=NULL) // free previous classifier
				{
					FREE(classifier->c0);
					FREE(classifier->c1);
					FREE(classifier->c2);
					FREE(classifier);
				}
				classifier=current;
			}
			else
			{
				FREE(current->c0);
				FREE(current->c1);
				FREE(current->c2);
				FREE(current);
			}
		}
#endif
		vector_push(classifiers,classifier);
		double error=compute_classification_error(classifiers, examples, sum_of_weights, classes->length); // compute error rate and update weights
		double dev_error=NAN;
		if(dev_examples!=NULL)dev_error = compute_classification_error(classifiers, dev_examples, NULL, classes->length); // compute error rate on dev
		double test_error=NAN;
		if(test_examples!=NULL)test_error = compute_classification_error(classifiers, test_examples, NULL, classes->length); // compute error rate on test
		// display result "a la" boostexter
		if(verbose==1)
		{
			char* token="";
			if(classifier->type==CLASSIFIER_TYPE_TEXT)
			{
				tokeninfo_t* tokeninfo=(tokeninfo_t*)vector_get(classifier->template->tokens,classifier->token);
				token=tokeninfo->key;
			}
			fprintf(stdout,"\n%s:%s\n",classifier->template->name->data,token);
			if(classifier->type==CLASSIFIER_TYPE_THRESHOLD)
			{
				fprintf(stdout,"Threshold:% 7g\nC0: ",classifier->threshold);
				for(i=0;i<classes->length;i++)fprintf(stdout,"% 4.3f ",classifier->c0[i]); fprintf(stdout,"\n");
			}
			//fprintf(stdout,"round %d type %d column %d alpha %f threshold %f token %s objective %f ", iteration, classifier->type,classifier->column,classifier->alpha,classifier->threshold, token, classifier->objective);
			//fprintf(stdout,"dev-error %f test-error %f train-error %f ",dev_error,test_error,error);
			//fprintf(stdout,"\n");
			//for(i=0;i<classes->length;i++)fprintf(stdout,"%f ",classifier->c0[i]); fprintf(stdout,"\n");
			fprintf(stdout,"C1: ");for(i=0;i<classes->length;i++)fprintf(stdout,"% 4.3f ",classifier->c1[i]); fprintf(stdout,"\n");
			fprintf(stdout,"C2: ");for(i=0;i<classes->length;i++)fprintf(stdout,"% 4.3f ",classifier->c2[i]); fprintf(stdout,"\n");
		}
		theorical_error*=classifier->objective;
		fprintf(stdout,"rnd %4d: wh-err= %.6f  th-err= %.6f  dev= %.7f  test= %.7f  train= %.7f\n",iteration+1,classifier->objective,theorical_error,dev_error,test_error,error);
		// unlike boostexter, C0 is always unk, C1 below or absent, C2 above or present
	}

	// release data structures (this is why we need a garbage collector ;)
	FREE(sum_of_weights[0]);
	FREE(sum_of_weights[1]);
	FREE(sum_of_weights);

	for(i = 0; i < classifiers->length; i++)
	{
		weakclassifier_t* classifier=(weakclassifier_t*)vector_get(classifiers,i);
		FREE(classifier->c0);
		FREE(classifier->c1);
		FREE(classifier->c2);
		FREE(classifier);
	}
	vector_free(classifiers);
	for(i=0; i<classes->length; i++) string_free((string_t*)vector_get(classes,i));
	vector_free(classes);
	for(i=0; i<templates->length; i++)
	{
		template_t* template=(template_t*)vector_get(templates,i);
		string_free(template->name);
		hashtable_free(template->dictionary);
		int j;
		for(j=0; j<template->tokens->length; j++)
		{
			tokeninfo_t* tokeninfo = (tokeninfo_t*) vector_get(template->tokens, j);
			FREE(tokeninfo->key);
			FREE(tokeninfo);
		}
		vector_free(template->tokens);
		if(template->ordered!=NULL)vector_free(template->ordered);
		FREE(template);
	}
	vector_free(templates);
	if(dev_examples!=NULL)
	{
		for(i=0; i<dev_examples->length; i++)
		{
			example_t* example=(example_t*)vector_get(dev_examples,i);
			vector_free(example->features);
			FREE(example->weight);
			FREE(example->score);
			FREE(example);
		}
		vector_free(dev_examples);
	}
	if(test_examples!=NULL)
	{
		for(i=0; i<test_examples->length; i++)
		{
			example_t* example=(example_t*)vector_get(test_examples,i);
			vector_free(example->features);
			FREE(example->weight);
			FREE(example->score);
			FREE(example);
		}
		vector_free(test_examples);
	}
	for(i=0; i<examples->length; i++)
	{
		example_t* example=(example_t*)vector_get(examples,i);
		vector_free(example->features);
		FREE(example->weight);
		FREE(example->score);
		FREE(example);
	}
	vector_free(examples);
	string_free(stem);
#ifdef USE_THREADS
	LOCK(finished);
	finished=1;
	UNLOCK(finished);
	fprintf(stderr,"FINISHED!!!\n");
	for(i=0;i<number_of_workers;i++)
	{
		int j;
		for(j=0;j<number_of_workers;j++)
			sem_post(toolbox->ready_to_process); // need more, because you cannot unlock all at a time
		void* output;
		pthread_join(workers[i],&output);
	}
	sem_unlink("ready_to_process");
	sem_close(toolbox->ready_to_process);
	sem_unlink("results_available");
	sem_close(toolbox->result_available);
	FREE(toolbox->best_classifier->c0);
	FREE(toolbox->best_classifier->c1);
	FREE(toolbox->best_classifier->c2);
	FREE(toolbox->best_classifier);
	FREE(toolbox);
#endif
	return 0;
}
