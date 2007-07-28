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

#define USE_THREADS
//#define USE_FLOATS

#include "utils/common.h"
#include "utils/vector.h"
#include "utils/string.h"
#include "utils/hashtable.h"
#include "utils/mapped.h"
#include "utils/array.h"
//#include "version.h"

#ifdef USE_THREADS
#include "utils/threads.h"
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
#include <time.h>

#ifdef USE_FLOATS
#define double float
#endif

// declare vectors that are interesting for us
vector_implement_functions_for_type(float, 0.0);
vector_implement_functions_for_type(int32_t, 0);


/*int num_EXP=0;
double EXP(double number)
{
	num_EXP++;
	if(isnan(number))die("exp(): had a NAN");
	return exp(number);
}

int num_SQRT=0;
double SQRT(double number)
{
	num_SQRT++;
	if(isnan(number))die("sqrt(): had a NAN");
	return sqrt(number);
}

int num_LOG=0;
double LOG(double number)
{
	num_LOG++;
	if(isnan(number))die("log(): had a NAN");
	return log(number);
}*/

#define EXP(a) exp(a)
#define LOG(a) log(a)

inline double SQRT(double number)
{
	if(number<0)return 0;
	return sqrt(number);
}

/*
WARNING:
- garbage collection is not working yet => do not activate it
TODO:
- free memory and remove leaks (and go through that again and again)
- frequency cutoff
- sampleing and ranking
- more COMMENTS !!!
NOTES ON THE ORIGINAL:
- text features are treated as bag-of-ngrams
- shyp file: boostexter saves non-text discrete features as their id
- shyp file: when combining the same classifiers, boostexter adds the alphas and averages the C_js
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
	vector_t* tokens;              // the actual tokens (if the feature is text; contains tokeninfo)
	vector_t* values;              // continuous values (if the feature is continuous)
	int32_t* ordered;             // ordered example ids according to value for continuous columns
	vector_t* classifiers;         // classifiers related to that template (in classification mode only)
} template_t;

typedef struct example { // an instance
	//vector_t* features;            // vector of (int or float according to templates)
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
	vector_t* examples;
} tokeninfo_t;

double smoothing=0.5;              // -E <number> in boostexter
int verbose=0;
int output_weights=0;
int output_scores=0;

//int *random_sequence;

#define y_l(x,y) (x->class==y?1.0:-1.0)    // y_l() from the paper
#define b(x,y) (x->class==y?1:0)           // b() from the paper (binary class match)

weakclassifier_t* train_text_stump(double min_objective, template_t* template, vector_t* examples, double** sum_of_weights, int num_classes)
{
	int i,l;
	int column=template->column;
	size_t num_tokens=template->tokens->length;
	int32_t t;

	double* weight[2][num_classes]; // weight[b][label][token] (=> D() in the paper), only for token presence (absence is infered from sum_of_weights)
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
		tokeninfo_t* tokeninfo=(tokeninfo_t*)vector_get(template->tokens, t);
		//fprintf(stdout,"%s [%s] %d\n",template->name->data,tokeninfo->key,tokeninfo->examples->length);
		for(i=0;i<tokeninfo->examples->length;i++) // compute the presence weights
		{
			example_t* example=(example_t*)vector_get(examples,vector_get_int32_t(tokeninfo->examples,i));
			for(l=0;l<num_classes;l++)
			{
				weight[b(example,l)][l][t]+=example->weight[l];
			}
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

	//min_objective=1;
	for(t=1;t<num_tokens;t++)
	{
		double objective=0;
		for(l=0;l<num_classes;l++) // compute the objective function Z()=sum_j(sum_l(SQRT(W+*W-))
		{
			/*if(weight[0][l][t]<0)weight[0][l][t]=0.0;
			if(weight[1][l][t]<0)weight[1][l][t]=0.0;*/
			objective+=SQRT((sum_of_weights[1][l]-weight[1][l][t])*(sum_of_weights[0][l]-weight[0][l][t]));
			objective+=SQRT(weight[1][l][t]*weight[0][l][t]);
		}
		objective*=2;
		//fprintf(stdout,"DEBUG: column=%d token=%d obj=%f\n",column,t,objective);
		if(objective-min_objective<-1e-11) // select the argmin()
		{
			min_objective=objective;
			classifier->token=t;
			classifier->objective=objective;
			for(l=0;l<num_classes;l++)  // update c0, c1 and c2 => c0 and c1 are the same for text stumps
			{
				classifier->c0[l]=0.5*LOG((sum_of_weights[1][l]-weight[1][l][t]+epsilon)/(sum_of_weights[0][l]-weight[0][l][t]+epsilon));
				classifier->c1[l]=classifier->c0[l];
				//classifier->c0[l]=0;
				//classifier->c1[l]=0.5*LOG((sum_of_weights[1][l]-weight[1][l][t]+epsilon)/(sum_of_weights[0][l]-weight[0][l][t]+epsilon));
				classifier->c2[l]=0.5*LOG((weight[1][l][t]+epsilon)/(weight[0][l][t]+epsilon));
			}
		}
	}
	for(l=0;l<num_classes;l++) // free memory
	{
		free(weight[0][l]);
		free(weight[1][l]);
	}
	//tokeninfo_t* info=vector_get(template->tokens,classifier->token);
	//fprintf(stdout,"DEBUG: column=%d token=%s obj=%f %s\n",column,info->key,classifier->objective,template->name->data);
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

weakclassifier_t* train_continuous_stump(double min_objective, template_t* template, vector_t* examples_vector, int num_classes)
{
	size_t i,j,l;
	int column=template->column;
	float* values=(float*)template->values->data;
	int32_t* ordered=template->ordered;
	example_t** examples=(example_t**)examples_vector->data;
	if(ordered==NULL) // only order examples once, then keep the result in the template
	{
		ordered=MALLOC(sizeof(int32_t)*examples_vector->length);
		int32_t index=0;
		for(index=0;index<examples_vector->length;index++)ordered[index]=index;
		//for(i=0;i<examples->length && i<4;i++)fprintf(stdout,"%d %f\n",i,vector_get_float(((example_t*)vector_get(examples,i))->features,column));
		int local_comparator(const void* a, const void* b)
		{
			int32_t aa=*(int32_t*)a;
			int32_t bb=*(int32_t*)b;
			float aa_value=values[aa];
			float bb_value=values[bb];
			//float aa_value=vector_get_float(template->values,(size_t)aa);
			//float bb_value=vector_get_float(template->values,(size_t)bb);
			//float aa_value=vector_get_float(((example_t*)vector_get(examples,(size_t)aa))->features,column);
			//float bb_value=vector_get_float(((example_t*)vector_get(examples,(size_t)bb))->features,column);
			//fprintf(stdout,"%d(%f) <=> %d(%f)\n",aa,aa_value,bb,bb_value);
			//if(aa_value<bb_value)return -1;
			//if(aa_value>bb_value)return 1;
			if(isnan(aa_value) || aa_value>bb_value)return 1; // put the NAN (unknown values) at the end of the list
			if(isnan(bb_value) || aa_value<bb_value)return -1;
			return 0;
		}
		qsort(ordered,examples_vector->length,sizeof(int32_t),local_comparator);
		//vector_sort(ordered,local_comparator);
		template->ordered=ordered;
	}

	double weight[3][2][num_classes]; // D(j,b,l)
	for(j=0;j<3;j++)
		for(l=0;l<num_classes;l++)
		{
			weight[j][0][l]=0.0;
			weight[j][1][l]=0.0;
		}
	for(i=0;i<examples_vector->length;i++) // compute the "unknown" weights and the weight of examples after threshold
	{
		int32_t example_id=ordered[i];
		example_t* example=examples[example_id];
		//fprintf(stdout,"%d %f\n",column,vector_get_float(example->features,column));
		for(l=0;l<num_classes;l++)
		{
			if(isnan(values[example_id]))
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
	double epsilon=smoothing/(num_classes*examples_vector->length);

	for(i=0;i<examples_vector->length-1;i++) // compute the objective function at every possible threshold (in between examples)
	{
		int32_t example_id=ordered[i];
		example_t* example=examples[example_id];
		//fprintf(stdout,"%zd %zd %f\n",i,vector_get_int32_t(ordered,i),vector_get_float(template->values,example_id));
		if(isnan(values[example_id]))break; // skip unknown values
		//example_t* next_example=(example_t*)vector_get(examples,(size_t)next_example_id);
		for(l=0;l<num_classes;l++) // update the objective function by putting the current example the other side of the threshold
		{
			weight[1][b(example,l)][l]+=example->weight[l];
			weight[2][b(example,l)][l]-=example->weight[l];
		}
		int next_example_id=ordered[i+1];
		if(values[example_id]==values[next_example_id])continue; // same value
		double objective=0;
		for(l=0;l<num_classes;l++) // compute objective Z()
		{
			/*if(weight[0][1][l]<0)weight[0][1][l]=0.0;
			if(weight[0][0][l]<0)weight[0][0][l]=0.0;
			if(weight[1][1][l]<0)weight[1][1][l]=0.0;
			if(weight[1][0][l]<0)weight[1][0][l]=0.0;
			if(weight[2][1][l]<0)weight[2][1][l]=0.0;
			if(weight[2][0][l]<0)weight[2][0][l]=0.0;*/
			objective+=SQRT(weight[0][1][l]*weight[0][0][l]);
			objective+=SQRT(weight[1][1][l]*weight[1][0][l]);
			objective+=SQRT(weight[2][1][l]*weight[2][0][l]);
		}
		objective*=2;
		//fprintf(stdout,"DEBUG: column=%d threshold=%f obj=%f\n",column,(vector_get_float(next_example->features,column)+vector_get_float(example->features,column))/2,objective);
		if(objective-min_objective<-1e-11) // get argmin
		{
			classifier->objective=objective;
			classifier->threshold=((double)values[next_example_id]+(double)values[example_id])/2.0; // threshold between current and next example
			if(isnan(classifier->threshold))die("threshold is nan, column=%d, objective=%f, i=%zd",column,objective,i); // should not happend
			//fprintf(stdout," %d:%d:%f",column,i,classifier->threshold);
			min_objective=objective;
			for(l=0;l<num_classes;l++) // update class weight
			{
				classifier->c0[l]=0.5*LOG((weight[0][1][l]+epsilon)/(weight[0][0][l]+epsilon));
				classifier->c1[l]=0.5*LOG((weight[1][1][l]+epsilon)/(weight[1][0][l]+epsilon));
				classifier->c2[l]=0.5*LOG((weight[2][1][l]+epsilon)/(weight[2][0][l]+epsilon));
			}
		}
	}
	//fprintf(stdout,"DEBUG: column=%d threshold=%f obj=%f %s\n",column,classifier->threshold,classifier->objective,template->name->data);
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
	//sem_t* ready_to_process; // if available, a thread can start working
	semaphore_t* ready_to_process;
	//sem_t* result_available; // if available, a result is avaiable in best_classifier
	semaphore_t* result_available;
} workertoolbox_t;


workertoolbox_t* toolbox=NULL;
//SHARED(int,next_worker_num,0);

void* threaded_worker(void* data)
{
	int column;
	/*LOCK(next_worker_num);
	int worker_num=next_worker_num;
	next_worker_num++;
	UNLOCK(next_worker_num);*/
	while(!finished)
	{
		//sem_wait(toolbox->ready_to_process);
		//if(verbose)fprintf(stdout,"%d worker thread ready\n",worker_num);
		semaphore_eat(toolbox->ready_to_process);
		if(finished)pthread_exit(NULL);
		LOCK(toolbox->next_column);
		column=toolbox->next_column;
		toolbox->next_column++;
		UNLOCK(toolbox->next_column);
		template_t* template=(template_t*)vector_get(toolbox->templates,column);
		//if(verbose)fprintf(stdout,"%d worker thread processing: %s\n",worker_num,template->name->data);
		//----------- do the job
		weakclassifier_t* current=NULL;
		if(template->type==FEATURE_TYPE_CONTINUOUS)
		{
			current=train_continuous_stump(1.0, template, toolbox->examples, toolbox->classes->length);
		}
		else if(template->type==FEATURE_TYPE_TEXT || template->type==FEATURE_TYPE_SET)
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
		//sem_post(toolbox->result_available);
		semaphore_feed(toolbox->result_available);
	}
	pthread_exit(NULL);
}
#endif

/* compute error rate AND update weights
   in testing conditions (dev or test set) sum_of_weights is NULL, so just compute error rate
   => need to be parallelized
*/
double compute_classification_error(vector_t* classifiers, vector_t* examples, double** sum_of_weights, int num_classes)
{
	int i=0;
	int l=0;
	double error=0;
	double normalization=0;
	weakclassifier_t* classifier=(weakclassifier_t*)vector_get(classifiers,classifiers->length-1);
	if(classifier->type==CLASSIFIER_TYPE_THRESHOLD)
	{
		for(i=0;i<examples->length;i++)
		{
			example_t* example=(example_t*)vector_get(examples,i);
			float value=vector_get_float(classifier->template->values,i);
			if(isnan(value))
			{
				for(l=0;l<num_classes;l++)
				{
					example->score[l]+=classifier->alpha*classifier->c0[l];
					example->weight[l]=example->weight[l]*EXP(-classifier->alpha*y_l(example,l)*classifier->c0[l]);
				}
			}
			else if(value<classifier->threshold)
			{
				for(l=0;l<num_classes;l++)
				{
					example->score[l]+=classifier->alpha*classifier->c1[l];
					example->weight[l]=example->weight[l]*EXP(-classifier->alpha*y_l(example,l)*classifier->c1[l]);
				}
			}
			else
			{
				for(l=0;l<num_classes;l++)
				{
					example->score[l]+=classifier->alpha*classifier->c2[l];
					example->weight[l]=example->weight[l]*EXP(-classifier->alpha*y_l(example,l)*classifier->c2[l]);
				}
			}
		}
	}
	else if(classifier->type==CLASSIFIER_TYPE_TEXT)
	{
		tokeninfo_t* tokeninfo=(tokeninfo_t*)vector_get(classifier->template->tokens,classifier->token);
		int* seen_examples=MALLOC(sizeof(int)*examples->length);
		memset(seen_examples,0,examples->length*sizeof(int));
		for(i=0;i<tokeninfo->examples->length;i++)
		{
			int32_t example_id=vector_get_int32_t(tokeninfo->examples,i);
			seen_examples[example_id]=1;
		}
		for(i=0;i<examples->length;i++)
		{
			example_t* example=(example_t*)vector_get(examples,i);
			if(seen_examples[i]==1)
			{
				for(l=0;l<num_classes;l++)
				{
					example->score[l]+=classifier->alpha*classifier->c2[l];
					example->weight[l]=example->weight[l]*EXP(-classifier->alpha*y_l(example,l)*classifier->c2[l]);
				}
			}
			else // unknown or absent (c1 = c0)
			{
				for(l=0;l<num_classes;l++)
				{
					example->score[l]+=classifier->alpha*classifier->c1[l];
					example->weight[l]=example->weight[l]*EXP(-classifier->alpha*y_l(example,l)*classifier->c1[l]);
				}
			}
		}
		FREE(seen_examples);
	}
	for(i=0;i<examples->length;i++)
	{
		example_t* example=(example_t*)vector_get(examples,i);
		double max=-1;
		int argmax=0;
		for(l=0;l<num_classes;l++) // selected class = class with highest score
		{
			normalization+=example->weight[l]; // update Z() normalization (not the same Z as in optimization)
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
		//double min_weight=examples->length*num_classes;
		//double max_weight=0;
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
			if(output_weights)fprintf(stdout,"iteration=%zd example=%d weights:\n",classifiers->length, i);
			for(l=0;l<num_classes;l++)
			{
				example->weight[l]/=normalization;
				if(output_weights)fprintf(stdout," %f",example->weight[l]);
				/*if(example->weight[l]<0)die("ERROR: negative weight: %d %d %f",i,l,example->weight[l]);
				if(min_weight>example->weight[l]){min_weight=example->weight[l];}
				if(max_weight<example->weight[l]){max_weight=example->weight[l];}*/
				//fprintf(stdout," %f",example->weight[l]);
				sum_of_weights[b(example,l)][l]+=example->weight[l];
			}
			if(output_weights)fprintf(stdout,"\n");
			//if(output_scores)fprintf(stdout,"XXX %f %f \n",example->score[0]/classifiers->length,example->score[1]/classifiers->length);
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
		//example->features = vector_new_type(templates->length,sizeof(int32_t)); // we will store 32bit ints and floats
		//example->features->length=templates->length;
		example->weight = NULL;
		char* current = begining_of_line;
		int i;
		for(i = 0; i < templates->length; i++) // get one feature per column
		{
			template_t* template = (template_t*)vector_get(templates,i);
			while(current < end_of_file && *current == ' ') current++; // strip spaces at begining
			if(current >= end_of_file) die("unexpected end of file, line %d, column %d (%s) in %s", line_num, i, template->name->data, filename);
			char* token = current;
			size_t length = 0;
			while(current < end_of_file && *current != ',') // get up to coma
			{
				current++;
				length++;
			}
			if(current >= end_of_file) die("unexpected end of file, line %d, column %d (%s) in %s", line_num, i, template->name->data, filename);
			while(*(token+length-1) == ' ' && length > 0) length--; // strip spaces as end
			char field[length+1];
			memcpy(field, token, length);
			field[length] = '\0';
			if(template->type == FEATURE_TYPE_CONTINUOUS)
			{
				float value=NAN;// unknwon is represented by Not-A-Number (NAN)
				char* error_location=NULL;
				if(strcmp(field,"?")) // if not unknown value
				{
					value = strtof(field, &error_location);
					if(error_location==NULL || *error_location!='\0')
					   die("could not convert \"%s\" to a number, line %d, char %td, column %d (%s) in %s", field, line_num, token-begining_of_line+1, i, template->name->data, filename);
				}
				vector_push_float(template->values,value);
			}
			else if(template->type == FEATURE_TYPE_TEXT || template->type==FEATURE_TYPE_SET)
			{
				if(strcmp(field,"?")) // if not unknwon value
				{
					char* word=NULL;
					hashtable_t* bag_of_words=hashtable_new();
					for(word=strtok(field, " "); word != NULL; word=strtok(NULL," "))
					{
						tokeninfo_t* tokeninfo = hashtable_get(template->dictionary, word, strlen(word));
						if(tokeninfo == NULL)
						{
							if(in_test)tokeninfo=vector_get(template->tokens,0); // default to the unknown token
							else if(template->type == FEATURE_TYPE_TEXT) // update the dictionary with the new token
							{
								tokeninfo = (tokeninfo_t*)MALLOC(sizeof(tokeninfo_t));
								tokeninfo->id = template->tokens->length;
								tokeninfo->key = strdup(word);
								tokeninfo->count=0;
								tokeninfo->examples=vector_new_int32_t(16);
								hashtable_set(template->dictionary, word, strlen(word), tokeninfo);
								vector_push(template->tokens, tokeninfo);
							}
							else die("value \"%s\" was not described in names file, line %d, column %d (%s) in %s", word, line_num, i, template->name->data, filename);
						}
						//vector_set_int32(example->features,i,tokeninfo->id);
						if(hashtable_get(bag_of_words, word, strlen(word))==NULL)
						{
							hashtable_set(bag_of_words, word, strlen(word), word);
							tokeninfo->count++;
							vector_push_int32_t(tokeninfo->examples,(int32_t)examples->length); // inverted index
						}
					}
					hashtable_free(bag_of_words);
				}
				else
				{
					//vector_set_int32(example->features,i,0); // unknown token is 0 (aka NULL)
				}
			}
			else
			{
				//vector_set_int32(example->features,i,0); // unsupported feature type: everything is unknown
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
			if(template->tokens->length>1)
			{
				for(j=1; j<template->tokens->length; j++) // remove unfrequent features
				{
					tokeninfo_t* tokeninfo=vector_get(template->tokens,j);
					tokeninfo->id=j;
					if(tokeninfo->count<feature_count_cutoff)
					{
						hashtable_remove(template->dictionary, tokeninfo->key, strlen(tokeninfo->key));
						if(tokeninfo->examples!=NULL)vector_free(tokeninfo->examples);
						FREE(tokeninfo->key);
						FREE(tokeninfo);
						memcpy(template->tokens->data+j*sizeof(void*),template->tokens->data+(template->tokens->length-1)*sizeof(void*),sizeof(void*));
						template->tokens->length--;
						j--;
					}
				}
			}
			vector_optimize(template->tokens);
			vector_optimize(template->values);
		}
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
	//if(verbose)fprintf(stdout,"EXAMPLES: %s %zd\n",filename, examples->length);
	mapped_free(input);
	return examples;
}

vector_t* load_model(vector_t* templates, vector_t* classes, char* filename)
{
	int i;
	vector_t* classifiers=vector_new(16);
	hashtable_t* templates_by_name=hashtable_new();
	for(i=0; i<templates->length; i++)
	{
		template_t* template=(template_t*)vector_get(templates,i);
		hashtable_set(templates_by_name, template->name->data, template->name->length, template);
		template->classifiers=vector_new(16);
	}
	mapped_t* input=mapped_load_readonly(filename);
	if(input==NULL) die("can't load model \"%s\"", filename);
	string_t* line=NULL;
	int line_num=0;
	int num_classifiers=-1;
	weakclassifier_t* current=NULL;
	while((line=mapped_readline(input))!=NULL)
	{
		line_num++;
		if(string_match(line,"^ *$","n")) // skip blank lines
		{
			string_free(line);
			continue;
		}
		if(num_classifiers==-1)
		{
			num_classifiers=string_to_int32(line);
		}
		else if(current==NULL)
		{
			vector_t* groups=string_match(line," *([^ ]+) Text:(SGRAM|THRESHOLD):([^:]+):(.*)",NULL);
			if(groups)
			{
				current=(weakclassifier_t*)MALLOC(sizeof(weakclassifier_t));
				string_t* template_name=vector_get(groups,3);
				template_t* template=hashtable_get(templates_by_name, template_name->data, template_name->length);
				if(template==NULL)die("invalid column template name \"%s\", line %d in %s", template_name->data, line_num, filename);
				string_t* alpha=vector_get(groups,1);
				current->alpha=string_to_double(alpha);
				if(isnan(current->alpha))die("invalid alpha value \"%s\", line %d in %s", alpha->data, line_num, filename);
				current->template=template;
				vector_push(classifiers, current);
				vector_push(current->template->classifiers,current);
				current->column=template->column;
				current->threshold=NAN;
				current->token=0;
				current->type=0;
				current->c0=NULL;
				current->c1=NULL;
				current->c2=NULL;
				string_t* word=vector_get(groups,4);
				if(string_eq_cstr(vector_get(groups,2),"SGRAM"))
				{
					current->type=CLASSIFIER_TYPE_TEXT;
					if(template->type==FEATURE_TYPE_SET)
					{
						int32_t token_num=string_to_int32(word)+1;
						if(token_num>template->tokens->length)die("invalid token number \"%s\", line %d in %s", word->data, line_num, filename);
						current->token=token_num;
					}
					else if(template->type==FEATURE_TYPE_TEXT)
					{
						tokeninfo_t* tokeninfo = hashtable_get(template->dictionary, word->data, word->length);
						if(tokeninfo==NULL)
						{
							tokeninfo = (tokeninfo_t*)MALLOC(sizeof(tokeninfo_t));
							tokeninfo->id = template->tokens->length;
							tokeninfo->key = strdup(word->data);
							tokeninfo->count=0;
							tokeninfo->examples=NULL;
							hashtable_set(template->dictionary, word->data, word->length, tokeninfo);
							vector_push(template->tokens, tokeninfo);
						}
						current->token = tokeninfo->id;
					}
				}
				else if(string_eq_cstr(vector_get(groups,2),"THRESHOLD"))
				{
					current->type=CLASSIFIER_TYPE_THRESHOLD;
				}
				else die("invalid classifier definition \"%s\", line %d in %s", line->data, line_num, filename);
				string_vector_free(groups);
			}
			else die("invalid classifier definition \"%s\", line %d in %s", line->data, line_num, filename);
		}
		else if(current->c0==NULL)
		{
			current->c0=MALLOC(sizeof(double)*classes->length);
			array_t* values=string_split(line," ",NULL);
			if(values==NULL || values->length!=classes->length)die("invalid weight distribution \"%s\", line %d in %s",line->data,line_num,filename);
			for(i=0; i<values->length; i++)
			{
				string_t* value=array_get(values,i);
				current->c0[i]=string_to_double(value);
				if(isnan(current->c0[i]))die("invalid value in distribution \"%s\", line %d in %s", value->data, line_num, filename);
			}
			string_array_free(values);
			if(current->type==CLASSIFIER_TYPE_TEXT)
			{
				current->c1=MALLOC(sizeof(double)*classes->length);
				memcpy(current->c1, current->c0, sizeof(double)*classes->length);
			}
		}
		else if(current->c1==NULL)
		{
			current->c1=MALLOC(sizeof(double)*classes->length);
			array_t* values=string_split(line," ",NULL);
			if(values==NULL || values->length!=classes->length)die("invalid weight distribution \"%s\", line %d in %s",line->data,line_num,filename);
			for(i=0; i<values->length; i++)
			{
				string_t* value=array_get(values,i);
				current->c1[i]=string_to_double(value);
				if(isnan(current->c1[i]))die("invalid value in distribution \"%s\", line %d in %s", value->data, line_num, filename);
			}
			string_array_free(values);
		}
		else if(current->c2==NULL)
		{
			current->c2=MALLOC(sizeof(double)*classes->length);
			array_t* values=string_split(line," ",NULL);
			if(values==NULL || values->length!=classes->length)die("invalid weight distribution \"%s\", line %d in %s",line->data,line_num,filename);
			for(i=0; i<values->length; i++)
			{
				string_t* value=array_get(values,i);
				current->c2[i]=string_to_double(value);
				if(isnan(current->c2[i]))die("invalid value in distribution \"%s\", line %d in %s", value->data, line_num, filename);
			}
			string_array_free(values);
			if(current->type==CLASSIFIER_TYPE_TEXT)
			{
				current=NULL;
			}
		}
		else if(current->type==CLASSIFIER_TYPE_THRESHOLD)
		{
			current->threshold=string_to_double(line);
			if(isnan(current->threshold))die("invalid threshold \"%s\", line %d in %s", line->data, line_num, filename);
			current=NULL;
		}
		else die("invalid classifier definition \"%s\", line %d in %s", line->data, line_num, filename);
		string_free(line);
	}
	//if(verbose)fprintf(stdout,"LOADED_CLASSIFIERS %zd\n",classifiers->length);
	mapped_free(input);
	hashtable_free(templates_by_name);
	for(i=0; i<templates->length; i++)
	{
		template_t* template=vector_get(templates, i);
		vector_optimize(template->classifiers);
	}
	vector_optimize(classifiers);
	return classifiers;
}

void save_model(vector_t* classifiers, vector_t* classes, char* filename, int pack_model)
{
	FILE* output=fopen(filename,"w");
	if(output==NULL)die("could not output model in \"%s\"",filename);
	int i;
	int num_classifiers=classifiers->length;
	if(pack_model)
	{
		hashtable_t* packed_classifiers=hashtable_new();
		for(i=0; i<classifiers->length; i++)
		{
			weakclassifier_t* classifier=vector_get(classifiers, i);
			string_t* identifier=string_sprintf("%d:%f:%d",classifier->column, classifier->threshold, classifier->token);
			weakclassifier_t* previous_classifier=hashtable_get(packed_classifiers, identifier->data, identifier->length);
			if(previous_classifier!=NULL)
			{
				int l;
				for(l=0;l<classes->length;l++)
				{
					previous_classifier->c0[l]+=classifier->c0[l];
					previous_classifier->c1[l]+=classifier->c1[l];
					previous_classifier->c2[l]+=classifier->c2[l];
				}
				previous_classifier->alpha+=classifier->alpha;
				FREE(classifier->c0);
				FREE(classifier->c1);
				FREE(classifier->c2);
				FREE(classifier);
				vector_set(classifiers, i ,NULL);
				num_classifiers--;
			}
			else
			{
				hashtable_set(packed_classifiers, identifier->data, identifier->length, classifier);
			}
			string_free(identifier);
		}
		hashtable_free(packed_classifiers);
	}
	fprintf(output,"%d\n\n",num_classifiers);
	for(i=0; i<classifiers->length; i++)
	{
		weakclassifier_t* classifier=(weakclassifier_t*)vector_get(classifiers,i);
		if(classifier==NULL)continue;
		fprintf(output,"   %.12f Text:",classifier->alpha);
		if(classifier->type==CLASSIFIER_TYPE_THRESHOLD)
		{
			fprintf(output,"THRESHOLD:%s:\n\n",classifier->template->name->data);
			int l=0;
			for(l=0;l<classes->length;l++) fprintf(output,"%.10f ",classifier->c0[l]/classifier->alpha); fprintf(output,"\n\n");
			for(l=0;l<classes->length;l++) fprintf(output,"%.10f ",classifier->c1[l]/classifier->alpha); fprintf(output,"\n\n");
			for(l=0;l<classes->length;l++) fprintf(output,"%.10f ",classifier->c2[l]/classifier->alpha); fprintf(output,"\n\n");
			fprintf(output,"%.10f\n\n\n",classifier->threshold);
		}
		else if(classifier->type==CLASSIFIER_TYPE_TEXT && classifier->template->type==FEATURE_TYPE_TEXT)
		{
			tokeninfo_t* tokeninfo=(tokeninfo_t*) vector_get(classifier->template->tokens,classifier->token);
			fprintf(output,"SGRAM:%s:%s\n\n",classifier->template->name->data,tokeninfo->key);
			int l=0;
			for(l=0;l<classes->length;l++) fprintf(output,"%.10f ",classifier->c1[l]/classifier->alpha); fprintf(output,"\n\n");
			for(l=0;l<classes->length;l++) fprintf(output,"%.10f ",classifier->c2[l]/classifier->alpha); fprintf(output,"\n\n");
			fprintf(output,"\n");
		}
		else if(classifier->type==CLASSIFIER_TYPE_TEXT && classifier->template->type==FEATURE_TYPE_SET)
		{
			tokeninfo_t* tokeninfo=(tokeninfo_t*) vector_get(classifier->template->tokens,classifier->token);
			fprintf(output,"SGRAM:%s:%d\n\n",classifier->template->name->data,tokeninfo->id-1); // 0 is unknown (?), so skip it
			int l=0;
			for(l=0;l<classes->length;l++) fprintf(output,"%.10f ",classifier->c1[l]/classifier->alpha); fprintf(output,"\n\n");
			for(l=0;l<classes->length;l++) fprintf(output,"%.10f ",classifier->c2[l]/classifier->alpha); fprintf(output,"\n\n");
			fprintf(output,"\n");
		}
		else die("unknown classifier type \"%d\"",classifier->type);
	}
	fclose(output);
}

void usage(char* program_name)
{
	fprintf(stderr,"USAGE: %s [options] -S <stem>\n",program_name);
	fprintf(stderr,"  --version               print version info\n");
	fprintf(stderr,"  -S <stem>               defines model/data/names stem\n");
	fprintf(stderr,"  -n <iterations>         number of boosting iterations\n");
	fprintf(stderr,"  -E <smoothing>          set smoothing value (default=0.5)\n");
	fprintf(stderr,"  -V                      verbose mode\n");
	fprintf(stderr,"  -C                      classification mode -- reads examples from <stdin>\n");
	fprintf(stderr,"  -o                      long output in classification mode\n");
	fprintf(stderr,"  --dryrun                only parse the names file and the data file to check for errors\n");
	fprintf(stderr,"  --cutoff <freq>         ignore nominal features occuring unfrequently\n");
	fprintf(stderr,"  --jobs <threads>        number of threaded weak learners\n");
	fprintf(stderr,"  --do-not-pack-model     do not pack model (to get individual training steps)\n");
	fprintf(stderr,"  --output-weights        output training examples weights at each iteration\n");
	fprintf(stderr,"  --model <model>         save/load the model to/from this file instead of <stem>.shyp\n");
	fprintf(stderr,"  --train <file>          bypass the <stem>.data filename to specify training examples\n");
	fprintf(stderr,"  --test <file>           output additional error rate from an other file during training (can be used multiple times, not implemented)\n");
	exit(1);
}

void print_version(char* program_name)
{
	fprintf(stdout,"%s v%s, Boosting decision stumps.\n", PACKAGE, VERSION);
	fprintf(stdout,"Written by Benoit Favre.\n\n");
	fprintf(stdout,"Copyright (C) 2007 International Computer Science Institute.\n");
	fprintf(stdout,"This is free software; see the source for copying conditions.  There is NO\n");
	fprintf(stdout,"warranty; not even for MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.\n\n");
	fprintf(stdout,"Build: %s at %s"
#ifdef __VERSION__
		", gcc %s\n",
#endif
		__DATE__,__TIME__,
#ifdef __VERSION__
		__VERSION__);
#endif
	fprintf(stdout,"Subversion info:\n");
	fprintf(stdout,"$URL$\n");
	fprintf(stdout,"$Date$\n");
	fprintf(stdout,"$Revision$\n");
	fprintf(stdout,"$Author$\n");
}

int main(int argc, char** argv)
{
#ifdef USE_GC
	GC_INIT();
#endif
	int maximum_iterations=10;
	int feature_count_cutoff=0;
	int classification_mode=0;
	int classification_output=0;
	int dryrun_mode=0;
	int pack_model=1;
	string_t* model_name=NULL;
	string_t* data_filename=NULL;
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
		else if(string_eq_cstr(arg,"--cutoff"))
		{
			//die("feature count cutoff not supported yet");
			string_free(arg);
			arg=(string_t*)array_shift(args);
			if(arg==NULL)die("value needed for -f");
			feature_count_cutoff=string_to_int32(arg);
			if(feature_count_cutoff<=0)die("invalid value for -f [%s]",arg->data);
		}
		else if(string_eq_cstr(arg,"-E"))
		{
			string_free(arg);
			arg=(string_t*)array_shift(args);
			if(arg==NULL)die("value needed for -E");
			smoothing=string_to_double(arg);
			if(isnan(smoothing) || smoothing<=0)die("invalid value for -E [%s]",arg->data);
		}
		else if(string_eq_cstr(arg,"--jobs"))
		{
#ifdef USE_THREADS
			string_free(arg);
			arg=(string_t*)array_shift(args);
			if(arg==NULL)die("value needed for --jobs");
			number_of_workers=string_to_int32(arg);
			if(number_of_workers<=0)die("invalid value for -jobs [%s]",arg->data);
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
		else if(string_eq_cstr(arg,"--version"))
		{
			print_version(argv[0]);
			exit(0);
		}
		else if(string_eq_cstr(arg,"-C"))
		{
			classification_mode=1;
		}
		else if(string_eq_cstr(arg,"-o"))
		{
			classification_output=1;
		}
		else if(string_eq_cstr(arg,"--model"))
		{
			string_free(arg);
			arg=(string_t*)array_shift(args);
			model_name=string_copy(arg);
		}
		else if(string_eq_cstr(arg,"--train"))
		{
			string_free(arg);
			arg=(string_t*)array_shift(args);
			data_filename=string_copy(arg);
		}
		else if(string_eq_cstr(arg,"--do-not-pack-model"))
		{
			pack_model=0;
		}
		else if(string_eq_cstr(arg,"--output-weights"))
		{
			output_weights=1;
		}
		else if(string_eq_cstr(arg,"--dryrun"))
		{
			dryrun_mode=1;
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
	hashtable_t* templates_by_name=hashtable_new();
	if(input == NULL) die("can't load \"%s\"", names_filename->data);
	string_t* line = NULL;
	int line_num = 0;
	while((line = mapped_readline(input)) != NULL) // should add some validity checking !!!
	{
		if(string_match(line,"^(\\|| *$)","n")) // skip comments and blank lines
		{
			string_free(line);
			continue;
		}
		if(classes != NULL) // this line contains a template definition
		{
			array_t* parts = string_split(line, "(^ +| *: *| *\\.$)", NULL);
			template_t* template = (template_t*)MALLOC(sizeof(template_t));
			template->column = line_num-1;
			template->name = (string_t*)array_get(parts, 0);
			string_t* type = (string_t*)array_get(parts, 1);
			template->dictionary = hashtable_new();
			template->tokens = vector_new(16);
			template->values = vector_new_float(16);
			template->classifiers = NULL;
			//template->dictionary_counts = vector_new(16);
			template->ordered=NULL;
			tokeninfo_t* unknown_token=(tokeninfo_t*)MALLOC(sizeof(tokeninfo_t));
			unknown_token->id=0;
			unknown_token->key=strdup("?");
			unknown_token->count=0;
			unknown_token->examples=NULL;
			vector_push(template->tokens,unknown_token);
			if(!strcmp(type->data, "continuous")) template->type = FEATURE_TYPE_CONTINUOUS;
			else if(!strcmp(type->data, "text")) template->type = FEATURE_TYPE_TEXT;
			else if(!strcmp(type->data, "scored text")) template->type = FEATURE_TYPE_IGNORE;
			else if(!strcmp(type->data, "ignore")) template->type = FEATURE_TYPE_IGNORE;
			else template->type = FEATURE_TYPE_SET;
			if(template->type == FEATURE_TYPE_SET)
			{
				array_t* values = string_split(type,"(^ +| *, *| *\\.$)", NULL);
				if(values->length <= 1)die("invalid column definition \"%s\", line %d in %s", line->data, line_num+1, names_filename->data);
				for(i=0; i<values->length; i++)
				{
					string_t* value=(string_t*)array_get(values,i);
					tokeninfo_t* tokeninfo=(tokeninfo_t*)MALLOC(sizeof(tokeninfo_t));
            		tokeninfo->id=i+1; // skip unknown value (?)
		            tokeninfo->key=strdup(value->data);
        		    tokeninfo->count=0;
					tokeninfo->examples=vector_new_int32_t(16);
					hashtable_set(template->dictionary, value->data, value->length, tokeninfo);
					vector_push(template->tokens,tokeninfo);
				}
				string_array_free(values);
			}

			if(hashtable_exists(templates_by_name, template->name->data, template->name->length)!=NULL)
				die("duplicate feature name \"%s\", line %d in %s",template->name->data, line_num+1, names_filename->data);
			vector_push(templates, template);
			hashtable_set(templates_by_name, template->name->data, template->name->length, template);
			string_free(type);
			array_free(parts);
			//if(verbose)fprintf(stdout,"TEMPLATE: %d %s %d\n",template->column,template->name->data,template->type);
		}
		else // first line contains the class definitions
		{
			array_t* parts = string_split(line, "(^ +| *, *| *\\.$)", NULL);
			if(parts->length <= 1)die("invalid classes definition \"%s\", line %d in %s", line->data, line_num+1, names_filename->data);
			classes = vector_from_array(parts);
			array_free(parts);
			/*if(verbose)
			{
				fprintf(stdout,"CLASSES:");
				for(i=0;i<classes->length;i++)fprintf(stdout," %s",((string_t*)vector_get(classes,i))->data);
				fprintf(stdout,"\n");
			}*/
		}
		string_free(line);
		line_num++;
	}
	vector_optimize(templates);
	mapped_free(input);
	hashtable_free(templates_by_name);
	string_free(names_filename);

	if(classification_mode)
	{
		vector_t* classifiers=NULL;
		if(model_name==NULL)
		{
			 model_name = string_copy(stem);
			string_append_cstr(model_name, ".shyp");
		}
		classifiers=load_model(templates,classes,model_name->data);
		double sum_of_alpha=0;
		int errors=0;
		int num_examples=0;
		for(i=0;i<classifiers->length;i++)
		{
			weakclassifier_t* classifier=vector_get(classifiers,i);
			sum_of_alpha+=classifier->alpha;
		}
		string_free(model_name);

		string_t* line=NULL;
		int line_num=0;
		while((line=string_readline(stdin))!=NULL)
		{
			line_num++;
			string_chomp(line);
			if(string_match(line,"^(\\|| *$)","n")) // skip comments and blank lines
			{
				string_free(line);
				continue;
			}
			int l;
			array_t* array_of_tokens=string_split(line, " *, *", NULL);
			if(array_of_tokens->length<templates->length || array_of_tokens->length>templates->length+1)
				die("wrong number of columns (%d), \"%s\", line %d in %s", array_of_tokens->length, line->data, line_num, "stdin");
			double score[classes->length];
			for(l=0; l<classes->length; l++) score[l]=0.0;
			for(i=0; i<templates->length; i++)
			{
				template_t* template=vector_get(templates, i);
				string_t* token=array_get(array_of_tokens, i);
				if(template->type == FEATURE_TYPE_TEXT || template->type == FEATURE_TYPE_SET)
				{
					hashtable_t* subtokens=hashtable_new();
					if(string_cmp_cstr(token,"?")!=0)
					{
						char* subtoken=NULL;
						for(subtoken=strtok(token->data, " "); subtoken != NULL; subtoken=strtok(NULL, " "))
						{
							tokeninfo_t* tokeninfo=hashtable_get(template->dictionary, subtoken, strlen(subtoken));
							if(tokeninfo!=NULL)
								hashtable_set(subtokens, &tokeninfo->id, sizeof(tokeninfo->id), tokeninfo);
						}
					}
					int j;
					for(j=0; j<template->classifiers->length; j++)
					{
						weakclassifier_t* classifier=vector_get(template->classifiers, j);
						if(hashtable_get(subtokens, &classifier->token, sizeof(classifier->token))==NULL)
							for(l=0; l<classes->length; l++) score[l]+=classifier->alpha*classifier->c1[l];
						else
							for(l=0; l<classes->length; l++) score[l]+=classifier->alpha*classifier->c2[l];
					}
					hashtable_free(subtokens);
				}
				else if(template->type == FEATURE_TYPE_CONTINUOUS)
				{
					float value = NAN;
					if(string_cmp_cstr(token,"?")!=0)value=string_to_float(token);
					int j;
					for(j=0; j<template->classifiers->length; j++)
					{
						weakclassifier_t* classifier=vector_get(template->classifiers, j);
						if(isnan(value))
							for(l=0; l<classes->length; l++) score[l]+=classifier->alpha*classifier->c0[l];
						else if(value < classifier->threshold)
							for(l=0; l<classes->length; l++) score[l]+=classifier->alpha*classifier->c1[l];
						else
							for(l=0; l<classes->length; l++) score[l]+=classifier->alpha*classifier->c2[l];
					}
				}
			}
			string_t* true_class=NULL;
			vector_t* tokens=array_to_vector(array_of_tokens);
			if(tokens->length > templates->length)
			{
				true_class=vector_get(tokens, tokens->length-1);
				while(true_class->data[true_class->length-1]=='.')
				{
					true_class->data[true_class->length-1]='\0';
					true_class->length--;
				}
			}
			for(l=0; l<classes->length; l++)score[l]/=sum_of_alpha;
			if(!dryrun_mode)
			{
				if(classification_output==0)
				{
					if(true_class!=NULL)
					{
						for(l=0; l<classes->length; l++)
						{
							string_t* class=vector_get(classes,l);
							if(string_cmp(class,true_class)==0) fprintf(stdout,"1 ");
							else fprintf(stdout,"0 ");
						}
					}
					else
					{
						for(l=0; l<classes->length; l++)fprintf(stdout,"? ");
					}
					for(l=0; l<classes->length; l++)
					{
						fprintf(stdout,"%.12f",score[l]);
						if(l<classes->length-1)fprintf(stdout," ");
					}
					fprintf(stdout,"\n");
				}
				else
				{
					fprintf(stdout,"\n\n");
					for(i=0; i<templates->length; i++)
					{
						template_t* template=vector_get(templates,i);
						string_t* token=vector_get(tokens,i);
						fprintf(stdout,"%s: %s\n", template->name->data, token->data);
					}
					if(true_class!=NULL)
					{
						fprintf(stdout,"correct label = %s \n",true_class->data);
						for(l=0; l<classes->length; l++)
						{
							string_t* class=vector_get(classes,l);
							fprintf(stdout,"%s%s % 5f : %s \n",string_cmp(true_class,class)==0?"*":" ",
									(score[l]>0?">":(string_cmp(true_class,class)==0?"*":" ")),score[l],class->data);
							if((score[l]<0 && string_cmp(true_class,class)==0))errors++;
						}
					}
					else
					{
						fprintf(stdout,"correct label = ?\n");
						for(l=0; l<classes->length; l++)
						{
							string_t* class=vector_get(classes,l);
							fprintf(stdout,"   % 5f : %s\n", score[l],class->data);
						}
					}
				}
			}
			vector_free(tokens);
			string_array_free(array_of_tokens);
			string_free(line);
			num_examples++;
		}
		if(!dryrun_mode && classification_output!=0)
		{
			fprintf(stderr,"ERROR RATE: %f\n",(double)errors/(double)num_examples);
		}
		exit(0);
	}

	// load a train, dev and test sets (if available)
	if(data_filename==NULL)
	{
		data_filename = string_copy(stem);
		string_append_cstr(data_filename, ".data");
	}
	vector_t* examples = load_examples(data_filename->data, templates, classes, feature_count_cutoff, 0);
	string_free(data_filename);

	// generate a simple random sequence of example ids for sampleing
	/*random_sequence=(int*)MALLOC(sizeof(int)*examples->length);
	srand(time(NULL));
	for(i=0;i<examples->length;i++)
	{
		int index=rand()%examples->length;
		int tmp=random_sequence[i];
		random_sequence[i]=random_sequence[index];
		random_sequence[index]=tmp;
	}*/

	vector_t* dev_examples=NULL;
	vector_t* test_examples=NULL;

// deactivated dev/test that don't work with the new indexed features (need separate handleing)

	/*data_filename = string_copy(stem);
	string_append_cstr(data_filename, ".dev");
	dev_examples = load_examples(data_filename->data, templates, classes, 0, 1);
	string_free(data_filename);

	data_filename = string_copy(stem);
	string_append_cstr(data_filename, ".test");
	test_examples = load_examples(data_filename->data, templates, classes, 0, 1);
	string_free(data_filename);*/

	if(dryrun_mode)exit(0);
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
	//sem_unlink("ready_to_process");
	//toolbox->ready_to_process=sem_open("ready_to_process",O_CREAT,0700,0);
	//if(toolbox->ready_to_process==(sem_t*)SEM_FAILED)die("ready to process %d",SEM_FAILED);
	toolbox->ready_to_process=semaphore_new(0);
	//toolbox->result_available=MALLOC(sizeof(sem_t));
	//sem_init(toolbox->result_available,0,0);
	//sem_unlink("result_available");
	//toolbox->result_available=sem_open("result_avaiable",O_CREAT,0700,0);
	//if(toolbox->result_available==(sem_t*)SEM_FAILED)die("result_available");
	toolbox->result_available=semaphore_new(0);
	semaphore_feed(toolbox->result_available);
	semaphore_eat(toolbox->result_available);
	for(i=0; i<number_of_workers; i++)
	{
		/*pthread_attr_t attributes;
		pthread_attr_init(&attributes);
		pthread_attr_setdetachstate(&attributes,PTHREAD_CREATE_JOINABLE);*/
		//pthread_create(&workers[i],&attributes,threaded_worker,NULL);
		pthread_create(&workers[i],NULL,threaded_worker,NULL);
		//pthread_attr_destroy(&attributes);
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
			//sem_post(toolbox->ready_to_process);
			semaphore_feed(toolbox->ready_to_process);
		}
		// need to store and reorder potential classifiers to get a deterministic behaviour
		for(i=0;i<templates->length;i++) // wait for the results
		{
			//sem_wait(toolbox->result_available);
			semaphore_eat(toolbox->result_available);
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
			else if(template->type==FEATURE_TYPE_TEXT || template->type==FEATURE_TYPE_SET)
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
		if(iteration==maximum_iterations-1) output_scores=1; else output_scores=0;
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
			if(classifier->type==CLASSIFIER_TYPE_THRESHOLD)
			{
				fprintf(stdout,"\n%s:%s\n",classifier->template->name->data,token);
				fprintf(stdout,"Threshold: %7.3g\n",classifier->threshold);
				classifier->c0[0]=-1e-11;
				fprintf(stdout,"C0: ");for(i=0;i<classes->length;i++)fprintf(stdout," % 4.3f ",classifier->c0[i]); fprintf(stdout,"\n");
				fprintf(stdout,"C1: ");for(i=0;i<classes->length;i++)fprintf(stdout," % 4.3f ",classifier->c1[i]); fprintf(stdout,"\n");
				fprintf(stdout,"C2: ");for(i=0;i<classes->length;i++)fprintf(stdout," % 4.3f ",classifier->c2[i]); fprintf(stdout,"\n");
			}
			else if(classifier->type==CLASSIFIER_TYPE_TEXT)
			{
				fprintf(stdout,"\n%s:%s \n",classifier->template->name->data,token);
				fprintf(stdout,"C0: ");for(i=0;i<classes->length;i++)fprintf(stdout," % 4.3f ",classifier->c1[i]); fprintf(stdout,"\n");
				fprintf(stdout,"C1: ");for(i=0;i<classes->length;i++)fprintf(stdout," % 4.3f ",classifier->c2[i]); fprintf(stdout,"\n");
			}
		}
		theorical_error*=classifier->objective;
		fprintf(stdout,"rnd %4d: wh-err= %.6f  th-err= %.6f  test= %7f  train= %7f\n",iteration+1,classifier->objective,theorical_error,test_error,error);
		//fprintf(stdout,"rnd %4d: wh-err= %.6f  th-err= %.6f  dev= %.7f  test= %.7f  train= %.7f\n",iteration+1,classifier->objective,theorical_error,dev_error,test_error,error);
		// unlike boostexter, C0 is always unk, C1 below or absent, C2 above or present
	}

	if(model_name==NULL)
	{
		model_name = string_copy(stem);
		string_append_cstr(model_name, ".shyp");
	}
	save_model(classifiers,classes,model_name->data,pack_model);
	string_free(model_name);

	// release data structures (this is why we need a garbage collector ;)
	FREE(sum_of_weights[0]);
	FREE(sum_of_weights[1]);
	FREE(sum_of_weights);

	for(i = 0; i < classifiers->length; i++)
	{
		weakclassifier_t* classifier=(weakclassifier_t*)vector_get(classifiers,i);
		if(classifier==NULL)continue;
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
			if(tokeninfo->examples!=NULL)vector_free(tokeninfo->examples);
			FREE(tokeninfo->key);
			FREE(tokeninfo);
		}
		vector_free(template->tokens);
		vector_free(template->values);
		if(template->ordered!=NULL)FREE(template->ordered);
		if(template->classifiers!=NULL)vector_free(template->classifiers);
		FREE(template);
	}
	vector_free(templates);
	if(dev_examples!=NULL)
	{
		for(i=0; i<dev_examples->length; i++)
		{
			example_t* example=(example_t*)vector_get(dev_examples,i);
			//vector_free(example->features);
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
			//vector_free(example->features);
			FREE(example->weight);
			FREE(example->score);
			FREE(example);
		}
		vector_free(test_examples);
	}
	for(i=0; i<examples->length; i++)
	{
		example_t* example=(example_t*)vector_get(examples,i);
		//vector_free(example->features);
		FREE(example->weight);
		FREE(example->score);
		FREE(example);
	}
	vector_free(examples);
	string_free(stem);
	//if(verbose)fprintf(stdout,"FINISHED!!!\n");
#ifdef USE_THREADS
	LOCK(finished);
	finished=1;
	UNLOCK(finished);
	for(i=0;i<number_of_workers;i++)
	{
		int j;
		for(j=0;j<number_of_workers;j++)
			semaphore_feed(toolbox->ready_to_process);
			//sem_post(toolbox->ready_to_process); // need more, because you cannot unlock all at a time
		void* output;
		pthread_join(workers[i],&output);
		//if(verbose)fprintf(stdout,"worker joined with return value: %p\n",output);
	}
	/*sem_unlink("ready_to_process");
	sem_close(toolbox->ready_to_process);
	sem_unlink("results_available");
	sem_close(toolbox->result_available);*/
	semaphore_free(toolbox->ready_to_process);
	semaphore_free(toolbox->result_available);
	FREE(toolbox->best_classifier->c0);
	FREE(toolbox->best_classifier->c1);
	FREE(toolbox->best_classifier->c2);
	FREE(toolbox->best_classifier);
	FREE(toolbox);
#endif
	return 0;
}
