/*
 * This file is subject to the terms and conditions defined in
 * file 'COPYING', which is part of this source code package.
 */

#if HAVE_CONFIG_H
# include <config.h>
#endif

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#define USE_THREADS
//#define USE_FLOATS
#define USE_CUTOFF

#include "utils/common.h"
#include "utils/debug.h"
#include "utils/vector.h"
#include "utils/string.h"
#include "utils/hashtable.h"
#include "utils/mapped.h"
#include "utils/array.h"
#include "utils/file.h"
#include "utils/sort_r.h"

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
#include <unistd.h>
#include <float.h>

// this is not recommended
#ifdef USE_FLOATS
#define double float
#endif

// declare vectors that are interesting for us
vector_implement_functions_for_type(float, 0.0);
vector_implement_functions_for_type(int32_t, 0);

int use_abstaining_text_stump = 0;
int use_known_continuous_stump = 0;
int enforce_anti_priors = 0;
int has_multiple_labels_per_example = 0;

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

#define SQRT(number) sqrt(number)
/*inline double SQRT(double number)
{
	if(number<0)return 0;
	return sqrt(number);
}*/

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
    int text_expert_type;
    int text_expert_length;
    int no_unk_ngrams;
    string_t* drop_regex;
    int feature_count_cutoff;
	hashtable_t* dictionary;       // dictionary for token based template
	vector_t* tokens;              // the actual tokens (if the feature is text; contains tokeninfo)
	vector_t* values;              // continuous values (if the feature is continuous)
	int32_t* ordered;             // ordered example ids according to value for continuous columns
	vector_t* classifiers;         // classifiers related to that template (in classification mode only)
} template_t;

typedef struct example { // an instance
	int32_t* classes;
	int num_classes;
	double* score;                 // example score by class, updated iteratively
	double* weight;                // example weight by class
	//vector_t* features;            // vector of (int or float according to templates)
} example_t;

typedef struct test_example { // a test instance (not very memory efficient) WARNING: has to be "compatible" with example_t up to score[]
	int32_t* classes;
	int num_classes;
	double* score;
	float* continuous_features;
	vector_t** discrete_features;
} test_example_t;

#define CLASSIFIER_TYPE_THRESHOLD 1
#define CLASSIFIER_TYPE_TEXT 2
#define CLASSIFIER_TYPE_EXTERNAL 3

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
	string_t* external_filename;          // in case the classifier is external, use decisions from a file
	double** external_train_decisions;    // - for training (format is one example per line, one score per class in the order of the names file)
	double** external_dev_decisions;      // - for development
	double** external_test_decisions;     // - for testing
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
//int output_scores = 0;

//int *random_sequence;

#define y_l(x,y) (x->classes[y] == 1?1.0:-1.0)    // y_l() from the paper
//double y_l(example_t* example, int label) { return example->classes[label] ? 1.0 : -1.0; }
/*inline double y_l(example_t* example, int label)
{
	int i;
	for(i=0; i<example->num_classes; i++)
	{
		if(example->classes[i] == label)return 1.0;
	}
	return -1.0;
}*/

#define b(x,y) (x->classes[y] == 1?1:0)           // b() from the paper (binary class match)
//int b(example_t* example, int label) { return example->classes[label] ? 1 : 0; }
/*inline int b(example_t* example, int label)
{
	int i;
	for(i=0; i<example->num_classes; i++)
	{
		if(example->classes[i] == label)return 1;
	}
	return 0;
}*/

weakclassifier_t* train_text_stump(double min_objective, template_t* template, vector_t* examples, double** sum_of_weights, int num_classes)
{
	int i,l;
	int column=template->column;
	size_t num_tokens=template->tokens->length;
	int32_t t;

	double* weight[2][num_classes]; // weight[b][label][token] (=> D() in the paper), only for token presence (absence is inferred from sum_of_weights)
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
		FREE(weight[0][l]);
		FREE(weight[1][l]);
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

weakclassifier_t* train_abstaining_text_stump(double min_objective, template_t* template, vector_t* examples, double** sum_of_weights, int num_classes)
{
	int i,l;
	int column=template->column;
	size_t num_tokens=template->tokens->length;
	int32_t t;

	double* weight[2][num_classes]; // weight[b][label][token] (=> D() in the paper), only for token presence (absence is inferred from sum_of_weights)
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
		double w0=0;
		for(l=0;l<num_classes;l++) // compute the objective function Z()=sum_j(sum_l(SQRT(W+*W-))
		{
			objective+=SQRT(weight[1][l][t]*weight[0][l][t]);
			//objective+=SQRT((sum_of_weights[1][l]-weight[1][l][t])*(sum_of_weights[0][l]-weight[0][l][t]));
			w0+=sum_of_weights[0][l]-weight[0][l][t]+sum_of_weights[1][l]-weight[1][l][t];
		}
		objective*=2;
		objective+=w0;
		//fprintf(stdout,"DEBUG: column=%d token=%d obj=%f w0=%f\n",column,t,objective, w0);
		if(objective-min_objective<-1e-11) // select the argmin()
		{
			min_objective=objective;
			classifier->token=t;
			classifier->objective=objective;
			for(l=0;l<num_classes;l++)  // update c0, c1 and c2 => c0 and c1 are the same for text stumps
			{
				classifier->c0[l]=0.0;
				classifier->c1[l]=0.0;
				classifier->c2[l]=0.5*LOG((weight[1][l][t]+epsilon)/(weight[0][l][t]+epsilon));
			}
		}
	}
	for(l=0;l<num_classes;l++) // free memory
	{
		FREE(weight[0][l]);
		FREE(weight[1][l]);
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

int local_comparator1(const void* _a, const void* _b, void* values)
{
    int32_t aa=*(int32_t*)_a;
    int32_t bb=*(int32_t*)_b;
    float aa_value=((float*)values)[aa];
    float bb_value=((float*)values)[bb];
    if(isnan(aa_value) || aa_value>bb_value)return 1; // put the NAN (unknown values) at the end of the list
    if(isnan(bb_value) || aa_value<bb_value)return -1;
    return 0;
}

weakclassifier_t* train_known_continuous_stump(double min_objective, template_t* template, vector_t* examples_vector, int num_classes)
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
		sort_r(ordered,examples_vector->length,sizeof(int32_t),local_comparator1, values);
		template->ordered=ordered;
	}

	double weight[3][2][num_classes]; // D(j,b,l)
	for(j=0;j<3;j++)
		for(l=0;l<num_classes;l++)
		{
			weight[j][0][l]=0.0;
			weight[j][1][l]=0.0;
		}
	//double sum_of_unknowns = 0;
	for(i=0;i<examples_vector->length;i++) // compute the "unknown" weights and the weight of examples after threshold
	{
		int32_t example_id=ordered[i];
		example_t* example=examples[example_id];
		//fprintf(stdout,"%d %f\n",column,vector_get_float(example->features,column));
		for(l=0;l<num_classes;l++)
		{
			if(isnan(values[example_id])) {
				//sum_of_unknowns += example->weight[l];
				weight[0][b(example,l)][l]+=example->weight[l];
			} else
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
		double w0 = 0.0;
		for(l=0;l<num_classes;l++) // compute objective Z()
		{
			//objective+=SQRT(weight[0][1][l]*weight[0][0][l]);
			objective+=SQRT(weight[1][1][l]*weight[1][0][l]);
			objective+=SQRT(weight[2][1][l]*weight[2][0][l]);
			w0 += weight[0][0][l]+weight[0][1][l];
		}
		objective*=2;
		objective+=w0;
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
				classifier->c0[l]=0.0;
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

int local_comparator2(const void* _a, const void* _b, void* values)
{
    int32_t aa=*(int32_t*)_a;
    int32_t bb=*(int32_t*)_b;
    float aa_value=((float*)values)[aa];
    float bb_value=((float*)values)[bb];
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
		sort_r(ordered,examples_vector->length,sizeof(int32_t),local_comparator2, values);
		//vector_sort(ordered,local_comparator2);
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

    double objective=0; // compute objective for threshold below any known value
    for(l=0;l<num_classes;l++) // compute objective Z()
    {
        double w0 = weight[0][1][l]*weight[0][0][l]; if(w0 > 0) objective += SQRT(w0);
        double w1 = weight[1][1][l]*weight[1][0][l]; if(w1 > 0) objective += SQRT(w1);
        double w2 = weight[2][1][l]*weight[2][0][l]; if(w2 > 0) objective += SQRT(w2);
    }
    objective*=2;
    if(objective-min_objective<-1e-11) // get argmin
    {
        classifier->objective=objective;
        classifier->threshold=-DBL_MAX; // -infinity

        min_objective=objective;
        for(l=0;l<num_classes;l++) // update class weight
        {
            classifier->c0[l]=0.5*LOG((weight[0][1][l]+epsilon)/(weight[0][0][l]+epsilon));
            classifier->c1[l]=0.5*LOG((weight[1][1][l]+epsilon)/(weight[1][0][l]+epsilon));
            classifier->c2[l]=0.5*LOG((weight[2][1][l]+epsilon)/(weight[2][0][l]+epsilon));
        }
    }
	for(i=0;i<examples_vector->length-1;i++) // compute the objective function at every possible threshold (in between examples)
	{
		int32_t example_id=ordered[i];
		int32_t next_example_id=ordered[i+1];
		example_t* example=examples[example_id];
		//fprintf(stdout,"%zd %zd %f\n",i,vector_get_int32_t(ordered,i),vector_get_float(template->values,example_id));
		if(isnan(values[example_id]) || isnan(values[next_example_id]))break; // skip unknown values
		//example_t* next_example=(example_t*)vector_get(examples,(size_t)next_example_id);
		for(l=0;l<num_classes;l++) // update the objective function by putting the current example the other side of the threshold
		{
			weight[1][b(example,l)][l]+=example->weight[l];
			weight[2][b(example,l)][l]-=example->weight[l];
		}
		if(values[example_id]==values[next_example_id])continue; // same value
		double objective=0;
		for(l=0;l<num_classes;l++) // compute objective Z()
		{
            double w0 = weight[0][1][l]*weight[0][0][l]; if(w0 > 0) objective += SQRT(w0);
            double w1 = weight[1][1][l]*weight[1][0][l]; if(w1 > 0) objective += SQRT(w1);
            double w2 = weight[2][1][l]*weight[2][0][l]; if(w2 > 0) objective += SQRT(w2);
		}
		objective*=2;
		//fprintf(stdout,"DEBUG: column=%d threshold=%f obj=%f\n",column,(vector_get_float(next_example->features,column)+vector_get_float(example->features,column))/2,objective);
		if(objective-min_objective<-1e-11) // get argmin
		{
			classifier->objective=objective;
			classifier->threshold=((double)values[next_example_id]+(double)values[example_id])/2.0; // threshold between current and next example

			// fix continuous features with null variance -> this behavior is not compatible with boostexter
			/*if(isnan(values[next_example_id])) classifier->threshold = values[example_id] + 1.0;
			if(isnan(values[example_id])) classifier->threshold = values[next_example_id];*/

			if(isnan(classifier->threshold))
				die("threshold is nan, column=%d \"%s\", objective=%f, i=%zd, example_id=%d (%f) next_example_id=%d (%f)",column, template->name->data, objective,i, example_id, (double)values[example_id], next_example_id, (double)values[next_example_id]); // should not happend
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
	//sem_t* result_available; // if available, a result is available in best_classifier
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
			if(use_known_continuous_stump)
				current=train_known_continuous_stump(1.0, template, toolbox->examples, toolbox->classes->length);
			else
				current=train_continuous_stump(1.0, template, toolbox->examples, toolbox->classes->length);
		}
		else if(template->type==FEATURE_TYPE_TEXT || template->type==FEATURE_TYPE_SET)
		{
			if(use_abstaining_text_stump)
				current=train_abstaining_text_stump(1.0, template, toolbox->examples, toolbox->sum_of_weights, toolbox->classes->length);
			else
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

/* compute max error rate */
double compute_max_error(vector_t* examples, int num_classes)
{
	double error=0;
	int i,l;
	for(i=0; i<examples->length; i++)
	{
		test_example_t* example = vector_get(examples, i);

		int max_class = 0;
		for(l=0;l<num_classes;l++) // selected class = class with highest score
		{
			if (example->score[l]>example->score[max_class]) max_class = l;
		}
		if (!b(example, max_class)) error++;
	}
	return error/(examples->length);
}

/* compute error rate AND update weights
   in testing conditions (dev or test set) sum_of_weights is NULL, so just compute error rate
   => need to be parallelized
*/
double compute_test_error(vector_t* classifiers, vector_t* examples, int classifier_id, int num_classes)
{
	int i;
	int l;
	double error=0;
	weakclassifier_t* classifier=vector_get(classifiers, classifier_id);
	for(i=0; i<examples->length; i++)
	{
		test_example_t* example = vector_get(examples, i);
		if(classifier->type == CLASSIFIER_TYPE_THRESHOLD)
		{
			float value=example->continuous_features[classifier->column];
			if(isnan(value))
			{
				//fprintf(stdout, "%d %f==NAN\n", i+1, value, classifier->threshold);
				for(l=0;l<num_classes;l++)
				{
					example->score[l]+=classifier->alpha*classifier->c0[l];
				}
			}
			else if(value<classifier->threshold)
			{
				//fprintf(stdout, "%d %f<%f\n", i+1, value, classifier->threshold);
				for(l=0;l<num_classes;l++)
					example->score[l]+=classifier->alpha*classifier->c1[l];
			}
			else
			{
				//fprintf(stdout, "%d %f>=%f\n", i+1, value, classifier->threshold);
				for(l=0;l<num_classes;l++)
					example->score[l]+=classifier->alpha*classifier->c2[l];
			}
		}
		else if(classifier->type == CLASSIFIER_TYPE_TEXT)
		{
			int j;
			int has_token=0;
			//tokeninfo_t* tokeninfo=(tokeninfo_t*) vector_get(classifier->template->tokens,classifier->token);
			if(example->discrete_features[classifier->column] != NULL)
				for(j=0; j<example->discrete_features[classifier->column]->length; j++)
				{
					if(vector_get_int32_t(example->discrete_features[classifier->column], j)==classifier->token)
					{
						has_token=1;
						break;
					}
				}
			if(has_token)
			{
				//fprintf(stdout, "%d has token %s\n", i, tokeninfo->key);
				for(l=0;l<num_classes;l++)
					example->score[l]+=classifier->alpha*classifier->c2[l];
			}
			else // unknown or absent (c1 = c0)
			{
				//fprintf(stdout, "%d not has token %s\n", i, tokeninfo->key);
				for(l=0;l<num_classes;l++)
					example->score[l]+=classifier->alpha*classifier->c1[l];
			}
		}
		int erroneous_example = 0;
		//if(i<10)fprintf(stdout,"%d %f %f\n", i, example->score[0], example->score[1]);
		for(l=0;l<num_classes;l++) // selected class = class with highest score
		{
			if(example->score[l]>0.0 && !b(example,l)) erroneous_example = 1;
			else if(example->score[l]<=0.0 && b(example,l)) erroneous_example = 1;
		}
		if(erroneous_example == 1) error++;
	}
	return error/(examples->length);
}

int example_score_comparator_class = 0;
double fmeasure_beta = 1;

int example_score_comparator(const void* a, const void* b)
{
	test_example_t* aa = *((test_example_t**)a);
	test_example_t* bb = *((test_example_t**)b);
	if(aa->score[example_score_comparator_class] > bb->score[example_score_comparator_class])
		return 1;
	else if(aa->score[example_score_comparator_class] < bb->score[example_score_comparator_class])
		return -1;
	return 0;
}

double compute_max_fmeasure(vector_t* working_examples, int class_of_interest, double* threshold, double *recall_output, double *precision_output)
{
	double maximum_fmeasure = -1;
	example_score_comparator_class = class_of_interest;
	vector_t* examples = vector_copy(working_examples);
	vector_sort(examples, example_score_comparator);
	int i;
	double true_below = 0;
	double total_true = 0;
	for(i=0; i<examples->length; i++)
	{
		test_example_t* example = vector_get(examples, i);
		if(b(example, class_of_interest)) total_true++;
	}
	double previous_value = NAN;
	for(i=0; i<examples->length; i++)
	{
		test_example_t* example = vector_get(examples, i);
		if(example->score[class_of_interest] != previous_value) {
			double precision = (total_true - true_below) / (examples->length - i);
			double recall = (total_true - true_below) / total_true;
			double fmeasure = fmeasure_beta * precision + recall > 0 ? (1 + fmeasure_beta) * recall * precision / (fmeasure_beta * precision + recall) : 0;
			if(fmeasure > maximum_fmeasure) {
				maximum_fmeasure = fmeasure;
				if(threshold != NULL) *threshold = (previous_value + example->score[class_of_interest]) / 2.0;
				if(recall_output != NULL) *recall_output = recall;
				if(precision_output != NULL) *precision_output = precision;
			}
			//fprintf(stdout, "EX: %d %f p=%f r=%f f=%f\n", i, example->score[class_of_interest], precision, recall, fmeasure);
			previous_value = example->score[class_of_interest];
		}
		if(b(example, class_of_interest)) true_below++;
	}
	vector_free(examples);
	return maximum_fmeasure;
}

double compute_classification_error(vector_t* classifiers, vector_t* examples, int classifier_id, double** sum_of_weights, int num_classes)
{
	int i=0;
	int l=0;
	double error=0;
	double normalization=0;
	weakclassifier_t* classifier=(weakclassifier_t*)vector_get(classifiers,classifier_id);
	if(classifier->type==CLASSIFIER_TYPE_THRESHOLD)
	{
		for(i=0;i<examples->length;i++)
		{
			example_t* example=(example_t*)vector_get(examples,i);
			float value=vector_get_float(classifier->template->values,i);
			if(isnan(value))
			{
				if(use_known_continuous_stump == 0)
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
		int* seen_examples=MALLOC(sizeof(int)*examples->length);
		memset(seen_examples,0,examples->length*sizeof(int));
		tokeninfo_t* tokeninfo=(tokeninfo_t*)vector_get(classifier->template->tokens,classifier->token);
		if(tokeninfo != NULL || tokeninfo->examples != NULL)
		{
			for(i=0;i<tokeninfo->examples->length;i++)
			{
				int32_t example_id=vector_get_int32_t(tokeninfo->examples,i);
				seen_examples[example_id]=1;
			}
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
				if(use_abstaining_text_stump == 0)
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
		int erroneous_example = 0;
		/*if(output_scores) {
			for(l=0;l<num_classes;l++) {
				fprintf(stdout,"%d ", example->classes[l]);
			}
		}
		if(output_scores) fprintf(stdout," scores:");*/
		for(l=0;l<num_classes;l++) // selected class = class with highest score
		{
			normalization+=example->weight[l]; // update Z() normalization (not the same Z as in optimization)
			if(example->score[l]>0.0 && !b(example,l)) erroneous_example = 1;
			else if(example->score[l]<=0.0 && b(example,l)) erroneous_example = 1;
			//if(output_scores) fprintf(stdout, " %f", example->score[l]/classifiers->length);
		}
		//if(output_scores) fprintf(stdout, " %s\n", erroneous_example ? "ERROR" : "OK" );
		if(erroneous_example == 1) error++;
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
		for(i=0;i<examples->length;i++) // normalize the weights and do some stats for debugging
		{
			example_t* example=(example_t*)vector_get(examples,i);
			//fprintf(stdout,"%d",i);
			if(output_weights)fprintf(stdout,"iteration=%zd example=%d weights:\n", (size_t)classifier_id+1, i);
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
		}
		//fprintf(stdout,"norm=%.12f min=%.12f max=%.12f\n",normalization,min_weight,max_weight);
	}
	return error/(examples->length);
}

#define TEXT_EXPERT_NGRAM 1
#define TEXT_EXPERT_SGRAM 2
#define TEXT_EXPERT_FGRAM 3
// warning: text experts SGRAM and FGRAM do not output anything if the window is larger than the actual number of words
array_t* text_expert(template_t* template, string_t* text)
{
	array_t* words=string_split(text, " ", NULL);
	array_t* output = words;
	if(template->text_expert_length>1 && words->length>1)
	{
        output = array_new();
        int i,j;
        if(template->text_expert_type==TEXT_EXPERT_NGRAM)
        {
            for(i=0; i<words->length; i++)
            {
                array_t* gram=array_new();
                for(j=0; j<template->text_expert_length && i+j<words->length ;j++)
                {
                    array_push(gram, array_get(words, i+j));
                    array_push(output, string_join_cstr("#", gram));
                }
                array_free(gram);
            }
        }
        else if(template->text_expert_type==TEXT_EXPERT_FGRAM)
        {
            for(i=0; i+template->text_expert_length-1<words->length; i++)
            {
                array_t* gram=array_new();
                for(j=0; j<template->text_expert_length && i+j<words->length ;j++)
                {
                    array_push(gram, array_get(words, i+j));
                }
                array_push(output, string_join_cstr("#", gram));
                array_free(gram);
            }
        }
        else if(template->text_expert_type==TEXT_EXPERT_SGRAM) // different results than boostexter
        {
            for(i=0; i<words->length; i++)
            {
                array_t* gram=array_new();
                array_push(gram, array_get(words, i));
                array_push(output, string_copy(array_get(words, i)));
                for(j=1; j<template->text_expert_length && i+j<words->length ;j++)
                {
                    array_push(gram, array_get(words, i+j));
                    array_push(output, string_join_cstr("#", gram));
                    array_pop(gram);
                }
                array_free(gram);
            }
        }
        else
        {
            die("unimplemented text expert");
        }
        string_array_free(words);
    }
    if(template->no_unk_ngrams)
    {
        array_t* no_unk=string_array_grep(output, "(^|#)unk(#|$)", "!");
        string_array_free(output);
        if(no_unk == NULL)
            return array_new();
        return no_unk;
    }
    if(template->drop_regex)
    {
        //if(verbose) fprintf(stderr, "DROP(%s) /%s/\n", template->name->data, template->drop_regex->data);
        array_t* filtered=string_array_grep(output, template->drop_regex->data, "!");
        if(filtered == NULL) filtered = array_new();
        if(verbose && filtered->length != output->length) {
            string_t* before = string_join_cstr(" ", output);
            string_t* after = string_join_cstr(" ", filtered);
            fprintf(stderr, "DROP [%s] => [%s]\n", before->data, after->data);
        }
        string_array_free(output);
        return filtered;
    }
    return output;
}

vector_t* load_examples_multilabel(const char* filename, vector_t* templates, vector_t* classes, double* class_priors, 
	int feature_count_cutoff, int in_test)
{
	FILE* fp=stdin;
	if(strcmp(filename,"-")!=0)
	{
		fp=fopen(filename,"r");
		if(fp == NULL)
		{
			warn("can't load \"%s\"", filename);
			return NULL;
		}
	}
	string_t* line;
	vector_t* examples = vector_new(16);
	int line_num=0;
	int i,j;
	while((line=string_readline(fp))!=NULL)
	{
		if(line_num % 1000 == 0)fprintf(stderr, "\r%s: %d", filename, line_num);
		line_num++;
		string_chomp(line);
		if(string_match(line,"^(\\|| *$)","n")) // skip comments and blank lines
		{
			string_free(line);
			continue;
		}
		array_t* array_of_tokens=string_split(line, " *, *", NULL);
		if(array_of_tokens->length != templates->length+1)
			die("wrong number of columns (%zd), \"%s\", line %d in %s", array_of_tokens->length, line->data, line_num, filename);
		test_example_t* test_example=NULL;
		example_t* example=NULL;
		if(in_test)
		{
			test_example=MALLOC(sizeof(test_example_t));
			test_example->score=MALLOC(sizeof(double)*classes->length);
			for(i=0; i<classes->length; i++)test_example->score[i]=0.0;
			test_example->continuous_features=MALLOC(sizeof(float)*templates->length);
			memset(test_example->continuous_features,0,sizeof(float)*templates->length);
			test_example->discrete_features=MALLOC(sizeof(vector_t*)*templates->length);
			memset(test_example->discrete_features,0,sizeof(vector_t*)*templates->length);
			test_example->classes=NULL;
			test_example->num_classes=0;
		}
		else
		{
			example = MALLOC(sizeof(example_t));
			memset(example, 0, sizeof(example_t));
		}
		for(i=0; i<templates->length; i++)
		{
			template_t* template=vector_get(templates, i);
			string_t* token=array_get(array_of_tokens, i);
			//fprintf(stderr,"%d %d >>> %s\n", line_num, i, token->data);
			if(template->type == FEATURE_TYPE_CONTINUOUS)
			{
				float value=NAN;// unknwon is represented by Not-A-Number (NAN)
				char* error_location=NULL;
				if(token->length==0 || strcmp(token->data,"?")) // if not unknown value
				{
					value = strtof(token->data, &error_location);
					if(error_location==NULL || *error_location!='\0')
						die("could not convert \"%s\" to a number, line %d, column %d (%s) in %s", token->data, line_num, i, template->name->data, filename);
				}
				if(!in_test) vector_push_float(template->values,value);
				else test_example->continuous_features[template->column]=value;
			}
			else if(template->type == FEATURE_TYPE_TEXT || template->type==FEATURE_TYPE_SET)
			{
				if(token->length==0 || strcmp(token->data,"?")) // if not unknown value
				{
					if(in_test)test_example->discrete_features[template->column]=vector_new_int32_t(16);
					hashtable_t* bag_of_words=hashtable_new();
					string_t* field_string=string_new(token->data);
					array_t* experts=text_expert(template, field_string);
                    if(template->type == FEATURE_TYPE_SET && experts->length != 1) {
                        die("column %d \"%s\" cannot handle space-separated value \"%s\", line %d in %s", i, template->name->data, field_string->data, line_num, filename);
                    }
					for(j=0; j<experts->length ; j++)
					{
						string_t* expert = array_get(experts, j);
						tokeninfo_t* tokeninfo = hashtable_get(template->dictionary, expert->data, expert->length);
						if(tokeninfo == NULL && !in_test)
						{
							if(in_test)tokeninfo=vector_get(template->tokens,0); // default to the unknown token
							else if(template->type == FEATURE_TYPE_TEXT) // || template->type == FEATURE_TYPE_SET) // update the dictionary with the new token
							{
								tokeninfo = (tokeninfo_t*)MALLOC(sizeof(tokeninfo_t));
								tokeninfo->id = template->tokens->length;
								tokeninfo->key = strdup(expert->data);
								tokeninfo->count=0;
								tokeninfo->examples=vector_new_int32_t(16);
								hashtable_set(template->dictionary, expert->data, expert->length, tokeninfo);
								vector_push(template->tokens, tokeninfo);
							}
							else die("value \"%s\" was not described in the .names file, line %d, column %d (%s) in %s", expert->data, line_num, i, template->name->data, filename);
						}
						//vector_set_int32(example->features,i,tokeninfo->id);
						if(tokeninfo!=NULL && hashtable_get(bag_of_words, expert->data, expert->length)==NULL)
						{
							hashtable_set(bag_of_words, expert->data, expert->length, expert->data);
							if(!in_test)
							{
								tokeninfo->count++;
								vector_push_int32_t(tokeninfo->examples,(int32_t)examples->length); // inverted index
							}
							else
							{
								vector_push_int32_t(test_example->discrete_features[template->column], tokeninfo->id);
							}
						}
					}
					string_array_free(experts);
					string_free(field_string);
					hashtable_free(bag_of_words);
					if(in_test)vector_optimize(test_example->discrete_features[template->column]);
				}
				else
				{
					//vector_set_int32(example->features,i,0); // unknown token is 0 (aka NULL)
				}
				// FEATURE_TYPE_IGNORE
			}
		}
		string_t* last_token = array_get(array_of_tokens, templates->length);
		array_t* array_of_labels = string_split(last_token, "( *\\.$|  *)", NULL);
		//string_t* tmp = string_join_cstr("#", array_of_labels);
		//fprintf(stderr,"classes [%s]\n", tmp->data);
		if(array_of_labels == NULL || array_of_labels->length<1)
			die("wrong class definition \"%s\", line %d in %s", last_token->data, line_num, filename);
        if(array_of_labels->length > 1) has_multiple_labels_per_example = 1;
		if(in_test)
		{
			//test_example->num_classes = array_of_labels->length;
			//test_example->classes = MALLOC(sizeof(int32_t) * test_example->num_classes);
			test_example->num_classes = classes->length;
			test_example->classes = MALLOC(sizeof(int32_t) * classes->length);
			memset(test_example->classes, 0, sizeof(int32_t) * classes->length);
		}
		else
		{
			//example->num_classes = array_of_labels->length;
			//example->classes = MALLOC(sizeof(int32_t) * example->num_classes);
			example->classes = MALLOC(sizeof(int32_t) * classes->length);
			example->num_classes = classes->length;
			memset(example->classes, 0, sizeof(int32_t) * classes->length);
		}
		for(i=0; i<array_of_labels->length; i++)
		{
			string_t* class = array_get(array_of_labels, i);
			for(j=0; j<classes->length; j++)
			{
				string_t* other = vector_get(classes, j);
				if(string_eq(class, other))
				{
					/*if(in_test) test_example->classes[i] = j;
					else example->classes[i] = j;*/
					if(in_test) test_example->classes[j] = 1;
					else example->classes[j] = 1;
					break;
				}
			}
			if(j == classes->length) die("undeclared class \"%s\", line %d in %s", class->data, line_num, filename);
		}
		string_array_free(array_of_labels);
		string_array_free(array_of_tokens);
		string_free(line);
		if(in_test) vector_push(examples, test_example);
		else vector_push(examples, example);
	}
	fprintf(stderr, "\r%s: %d\n", filename, line_num);
#ifdef USE_CUTOFF
	if(!in_test) 
	{
		for(i=0;i<templates->length;i++)
		{
			template_t* template=(template_t*)vector_get(templates,i);
			if(template->tokens->length>1)
			{
				for(j=1; j<template->tokens->length; j++) // remove infrequent features
				{
					tokeninfo_t* tokeninfo=vector_get(template->tokens,j);
					tokeninfo->id=j;
					if(tokeninfo->count<template->feature_count_cutoff)
					{
						if(verbose)fprintf(stderr, "CUTOFF: \"%s\" %zd < %d\n", tokeninfo->key, tokeninfo->count, template->feature_count_cutoff);
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
#endif
	// initialize weights and score
	if(!in_test)
	{
		if(class_priors != NULL)
		{
			int l;
			for(l=0; l<classes->length; l++) class_priors[l] = 0.0;
			for(i=0;i<examples->length;i++)
			{
				example_t* example=(example_t*)vector_get(examples,i);
				for(j=0; j<example->num_classes; j++)
				{
					if(example->classes[j]) class_priors[j] ++;
					//class_priors[example->classes[j]]++;
				}
			}
			for(l=0; l<classes->length; l++)
			{
				class_priors[l] /= examples->length;
				string_t* class = vector_get(classes, l);
				if(verbose) fprintf(stderr,"CLASS PRIOR: %s %f\n", class->data, class_priors[l]);
			}
		}
		double normalization=0.0;
		for(i=0;i<examples->length;i++)
		{
			example_t* example=(example_t*)vector_get(examples,i);
			//fprintf(stdout,"%d %d %p\n",i,(int)vector_get(example->features,0), example->weight);
			example->weight=(double*)MALLOC(classes->length*sizeof(double));
			example->score=(double*)MALLOC(classes->length*sizeof(double));
			for(j=0;j<classes->length;j++)
			{
				if(enforce_anti_priors == 1)
				{
					if(b(example,j)==0)
						example->weight[j]=class_priors[j];
					else
						example->weight[j]=1.0;
				}
				else
					example->weight[j]=1.0;
				normalization+=example->weight[j];
				example->score[j]=0.0;
			}
		}
		for(i=0;i<examples->length;i++)
		{
			example_t* example=(example_t*)vector_get(examples,i);
			for(j=0;j<classes->length;j++)
				example->weight[j]/=normalization;
		}
	}
	vector_optimize(examples);
	fclose(fp);
	return examples;
}

/* load a data file
  if it is a test or dev file (in_test=1) then do not update dictionaries
  note: real test files without a class in the end will fail (this is not classification mode)
*/
/*vector_t* load_examples(const char* filename, vector_t* templates, vector_t* classes, int feature_count_cutoff, int in_test)
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
		test_example_t* test_example=NULL;
		if(in_test)
		{
			test_example=MALLOC(sizeof(test_example_t));
			test_example->score=MALLOC(sizeof(double)*classes->length);
			test_example->continuous_features=MALLOC(sizeof(float)*templates->length);
			memset(test_example->continuous_features,0,sizeof(float)*templates->length);
			test_example->discrete_features=MALLOC(sizeof(vector_t*)*templates->length);
			memset(test_example->discrete_features,0,sizeof(vector_t*)*templates->length);
		}
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
			while(*(token+length-1) == ' ' && length > 0) length--; // strip spaces at end
			char field[length+1];
			memcpy(field, token, length);
			field[length] = '\0';
			if(template->type == FEATURE_TYPE_CONTINUOUS)
			{
				float value=NAN;// unknwon is represented by Not-A-Number (NAN)
				char* error_location=NULL;
				if(length==0 || strcmp(field,"?")) // if not unknown value
				{
					value = strtof(field, &error_location);
					if(error_location==NULL || *error_location!='\0')
					   die("could not convert \"%s\" to a number, line %d, char %td, column %d (%s) in %s", field, line_num, token-begining_of_line+1, i, template->name->data, filename);
				}
				if(!in_test)
					vector_push_float(template->values,value);
				else
					test_example->continuous_features[template->column]=value;
			}
			else if(template->type == FEATURE_TYPE_TEXT || template->type==FEATURE_TYPE_SET)
			{
				if(length==0 || strcmp(field,"?")) // if not unknwon value
				{
					if(in_test)test_example->discrete_features[template->column]=vector_new_int32_t(16);
					hashtable_t* bag_of_words=hashtable_new();
					string_t* field_string=string_new(field);
					array_t* experts=text_expert(template, field_string);
					for(j=0; j<experts->length ; j++)
					{
						string_t* expert = array_get(experts, j);
						tokeninfo_t* tokeninfo = hashtable_get(template->dictionary, expert->data, expert->length);
						if(tokeninfo == NULL && !in_test)
						{
							if(in_test)tokeninfo=vector_get(template->tokens,0); // default to the unknown token
							else if(template->type == FEATURE_TYPE_TEXT || template->type == FEATURE_TYPE_SET) // update the dictionary with the new token
							{
								tokeninfo = (tokeninfo_t*)MALLOC(sizeof(tokeninfo_t));
								tokeninfo->id = template->tokens->length;
								tokeninfo->key = strdup(expert->data);
								tokeninfo->count=0;
								tokeninfo->examples=vector_new_int32_t(16);
								hashtable_set(template->dictionary, expert->data, expert->length, tokeninfo);
								vector_push(template->tokens, tokeninfo);
							}
							else die("value \"%s\" was not described in the .names file, line %d, column %d (%s) in %s", expert->data, line_num, i, template->name->data, filename);
						}
						//vector_set_int32(example->features,i,tokeninfo->id);
						if(tokeninfo!=NULL && hashtable_get(bag_of_words, expert->data, expert->length)==NULL)
						{
							hashtable_set(bag_of_words, expert->data, expert->length, expert->data);
							if(!in_test)
							{
								tokeninfo->count++;
								vector_push_int32_t(tokeninfo->examples,(int32_t)examples->length); // inverted index
							}
							else
							{
								vector_push_int32_t(test_example->discrete_features[template->column], tokeninfo->id);
							}
						}
					}
					string_array_free(experts);
					string_free(field_string);
					hashtable_free(bag_of_words);
					if(in_test)vector_optimize(test_example->discrete_features[template->column]);
				}
				else
				{
					//vector_set_int32(example->features,i,0); // unknown token is 0 (aka NULL)
				}
			}
			else
			{
				//=> FEATURE_TYPE_IGNORE
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
		if(!in_test)
		{
			example_t* example = MALLOC(sizeof(example_t)); // that's one new example per line
			//example->features = vector_new_type(templates->length,sizeof(int32_t)); // we will store 32bit ints and floats
			//example->features->length=templates->length;
			example->weight = NULL;
			example->class = id;
			vector_push(examples, example); // store example
		}
		else
		{
			test_example->class = id;
			vector_push(examples, test_example);
		}
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
						if(verbose)fprintf(stderr, "CUTOFF: \"%s\" %zd < %d\n", tokeninfo->key, tokeninfo->count, feature_count_cutoff);
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
	if(!in_test)
	{
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
	}
	if(verbose)fprintf(stdout,"EXAMPLES: %s %zd\n",filename, examples->length);
	mapped_free(input);
	return examples;
}*/

vector_t* load_model(vector_t* templates, vector_t* classes, char* filename, int num_iterations_to_load)
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
    int warned_ngram_tokens = 0;
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
			if(num_iterations_to_load == -1) num_iterations_to_load = num_classifiers;
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
				current->objective=NAN;
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
							tokeninfo->examples=vector_new(1);
							hashtable_set(template->dictionary, word->data, word->length, tokeninfo);
							vector_push(template->tokens, tokeninfo);
						}
						current->token = tokeninfo->id;
                        if(strchr(word->data, '#') != NULL) {
                            if(!warned_ngram_tokens) {
                                warn("ngram-like tokens found in model, did you set the correct -W and -N options?");
                                warned_ngram_tokens = 1;
                            }
                        }
					}
                    if(verbose) fprintf(stderr, "  SGRAM(%s) \"%s\" (id=%d)\n", current->template->name->data, word->data, current->token);
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
				if(num_iterations_to_load > 0 && classifiers->length >= num_iterations_to_load){ string_free(line); break;} // clip classifiers
			}
		}
		else if(current->type==CLASSIFIER_TYPE_THRESHOLD)
		{
			current->threshold=string_to_double(line);
            if(verbose) fprintf(stderr, "  THRESHOLD(%s) %g\n", current->template->name->data, current->threshold);
			if(isnan(current->threshold))die("invalid threshold \"%s\", line %d in %s", line->data, line_num, filename);
			current=NULL;
			if(num_iterations_to_load > 0 && classifiers->length >= num_iterations_to_load){ string_free(line); break; } // clip classifiers
		}
		else die("invalid classifier definition \"%s\", line %d in %s", line->data, line_num, filename);
		string_free(line);
	}
	if(verbose)fprintf(stdout,"LOADED_CLASSIFIERS %zd\n",classifiers->length);
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

void save_model(vector_t* classifiers, vector_t* classes, char* filename, int pack_model, int optimal_iterations)
{
	if(file_test(filename, "f"))
	{
		string_t* backup_name = string_new(filename);
		string_append_cstr(backup_name, ".previous");
		if(rename(filename, backup_name->data) == -1)
		{
			warn("could not backup previous model to \"%s\"", backup_name->data);
		}
		string_free(backup_name);
	}
	FILE* output=fopen(filename,"w");
	if(output==NULL)die("could not output model in \"%s\"",filename);
	int i;
	int num_classifiers=classifiers->length;
	if(pack_model && optimal_iterations==0)
	{
		hashtable_t* packed_classifiers=hashtable_new();
		for(i=0; i<classifiers->length; i++)
		{
			//if(optimal_iterations!=0 && i>optimal_iterations+1)break;
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
	fprintf(output,"%d\n\n",optimal_iterations!=0 ? optimal_iterations : num_classifiers);
	for(i=0; i<classifiers->length; i++)
	{
		//if(optimal_iterations!=0 && i>optimal_iterations+1)break;
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
	fprintf(stderr,"  -n <iterations>         number of boosting iterations (also limits test time classifiers, if model is not packed)\n");
	fprintf(stderr,"  -E <smoothing>          set smoothing value (default=0.5)\n");
	fprintf(stderr,"  -V                      verbose mode\n");
	fprintf(stderr,"  -C                      classification mode -- reads examples from <stdin>\n");
	fprintf(stderr,"  -o                      long output in classification mode\n");
	fprintf(stderr,"  -N <text_expert>        choose a text expert between fgram, ngram and sgram (also \"<name>:text:expert_type=<x>\" in the .names)\n");
	fprintf(stderr,"  -W <ngram_length>       specify window length of text expert (also \"<name>:text:expert_length=<n>\" in .names)\n");
	fprintf(stderr,"  --dryrun                only parse the names file and the data file to check for errors\n");
	//fprintf(stderr,"  --dryrun-train          do everything but do not save model\n");
	fprintf(stderr,"  --cutoff <freq>         ignore nominal features occurring infrequently (also \"<name>:text:cutoff=<freq>\" in .names)\n");
    fprintf(stderr,"  --drop <regex>          drop text features that match a regular expression (also \"<name>:text:drop=<regex>\" in .names)\n");
	fprintf(stderr,"  --no-unk-ngrams         ignore ngrams that contain the \"unk\" token\n");
	fprintf(stderr,"  --jobs <threads>        number of threaded weak learners\n");
	fprintf(stderr,"  --do-not-pack-model     do not pack model (this is the default behavior)\n");
	fprintf(stderr,"  --pack-model            pack model (for boostexter compatibility)\n");
	fprintf(stderr,"  --output-weights        output training examples weights at each iteration\n");
	fprintf(stderr,"  --posteriors            output posterior probabilities instead of boosting scores\n");
	fprintf(stderr,"  --model <model>         save/load the model to/from this file instead of <stem>.shyp\n");
	fprintf(stderr,"  --resume                resume training from a previous model (can use another dataset for adaptation)\n");
	fprintf(stderr,"  --train <file>          bypass the <stem>.data filename to specify training examples\n");
	fprintf(stderr,"  --dev <file>            bypass the <stem>.dev filename to specify development examples\n");
	fprintf(stderr,"  --test <file>           bypass the <stem>.test filename to specify test examples\n");
	fprintf(stderr,"  --names <file>          use this column description file instead of <stem>.names\n");
	fprintf(stderr,"  --ignore <columns>      ignore a comma separated list of columns (synonym with \"ignore\" in names file)\n");
	fprintf(stderr,"  --ignore-regex <regex>  ignore columns that match a given regex\n");
	fprintf(stderr,"  --only <columns>        use only a comma separated list of columns (synonym with \"ignore\" in names file)\n");
	fprintf(stderr,"  --only-regex <regex>    use only columns that match a given regex\n");
	fprintf(stderr,"  --interruptible         save model after each iteration in case of failure/interruption\n");
	fprintf(stderr,"  --optimal-iterations    output the model at the iteration that minimizes dev error (or max fmeasure if specified)\n");
	fprintf(stderr,"  --max-fmeasure <class>  display maximum f-measure of specified class instead of error rate\n");
	fprintf(stderr,"  --fmeasure-beta <float> specify weight of recall compared to precision in f-measure\n");
	fprintf(stderr,"  --abstaining-stump      use abstain-on-absence text stump (experimental)\n");
	fprintf(stderr,"  --no-unknown-stump      use abstain-on-unknown continuous stump (experimental)\n");
	fprintf(stderr,"  --sequence              generate column __SEQUENCE_PREVIOUS from previous prediction at test time (experimental)\n");
	fprintf(stderr,"  --anti-prior            set initial weights to focus on classes with a lower prior (experimental)\n");
	//fprintf(stderr,"  --display-maxclass      display the classification rate obtained by selecting only one class by exemple, the one that obtains the maximum score (boostexter-compatible)\n");
	exit(1);
}

void print_version(char* program_name)
{
	fprintf(stdout,"%s v%s, Boosting decision stumps.\n", PACKAGE, VERSION);
	fprintf(stdout,"Written by Benoit Favre.\n\n");
	fprintf(stdout,"Copyright (C) 2007-2009 International Computer Science Institute.\n");
	fprintf(stdout,"This is free software; see the source for copying conditions.  There is NO\n");
	fprintf(stdout,"warranty; not even for MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.\n\n");
	fprintf(stdout,"Build: %s at %s"
#ifdef __VERSION__
		", gcc %s"
#endif
		", %s\n" ,__DATE__ ,__TIME__ ,
#ifdef __VERSION__
		__VERSION__ ,
#endif
#ifdef __x86_64__
		"64bit"
#else
		"32bit"
#endif
	);
	fprintf(stdout,"Subversion info:\n");
	fprintf(stdout,"$URL$\n"); // automatically updated by subversion
	fprintf(stdout,"$Date$\n"); // automatically updated by subversion
	fprintf(stdout,"$Revision$\n"); // automatically updated by subversion
	fprintf(stdout,"$Author$\n"); // automatically updated by subversion
}

int main(int argc, char** argv)
{
#ifdef _SET_NAN
	NAN = sqrt(-1);
#endif
#ifdef DEBUG
	init_debugging(argv[0],DEBUG_NON_INTERACTIVE);
#endif
#ifdef USE_GC
	GC_INIT();
#endif
	int display_maxclass_error=0;
	int maximum_iterations=10;
	int test_time_iterations=-1;
	int feature_count_cutoff=0;
	int classification_mode=0;
	int classification_output=0;
	int dryrun_mode=0;
	int pack_model=0;
	int text_expert_type=TEXT_EXPERT_NGRAM;
	int text_expert_length=1;
	int optimal_iterations=0;
	int use_max_fmeasure = 0;
	string_t* fmeasure_class = NULL;
	int fmeasure_class_id = -1;
	int save_model_at_each_iteration=0;
	int no_unk_ngrams=0;
	int sequence_classification = 0;
	int resume_training = 0;
	int output_posteriors = 0;
	string_t* model_name=NULL;
	string_t* data_filename=NULL;
	string_t* dev_filename=NULL;
	string_t* test_filename=NULL;
	string_t* names_filename=NULL;
	array_t* ignore_columns=NULL;
	string_t* ignore_regex=NULL;
	array_t* only_columns=NULL;
	string_t* only_regex=NULL;
    string_t* drop_regex=NULL;
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
			test_time_iterations=maximum_iterations;
			if(maximum_iterations<=0)die("invalid value for -n [%s]",arg->data);
		}
		else if(string_eq_cstr(arg,"--cutoff"))
		{
			warn("feature count cutoff is experimental");
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
		else if(string_eq_cstr(arg,"--abstaining-stump"))
		{
			use_abstaining_text_stump = 1;
		}
		else if(string_eq_cstr(arg,"--posteriors"))
		{
			output_posteriors = 1;
		}
		else if(string_eq_cstr(arg,"--no-unknown-stump"))
		{
			use_known_continuous_stump = 1;
		}
		else if(string_eq_cstr(arg,"-V"))
		{
			verbose=1;
		}
		else if(string_eq_cstr(arg,"--interruptible"))
		{
			save_model_at_each_iteration=1;
		}
		else if(string_eq_cstr(arg,"--resume"))
		{
			resume_training = 1;
		}
		else if(string_eq_cstr(arg,"-S") && stem==NULL)
		{
			string_free(arg);
			arg=(string_t*)array_shift(args);
			if(arg==NULL)die("value needed for -S");
			stem=string_copy(arg);
		}
		else if(string_eq_cstr(arg,"--names"))
		{
			string_free(arg);
			arg=(string_t*)array_shift(args);
			if(arg==NULL)die("file name expected after --names");
			names_filename=string_copy(arg);
		}
		else if(string_eq_cstr(arg,"--ignore"))
		{
			string_free(arg);
			arg=(string_t*)array_shift(args);
			ignore_columns=string_split(arg,",",NULL);
		}
		else if(string_eq_cstr(arg,"--ignore-regex"))
		{
			string_free(arg);
			arg=(string_t*)array_shift(args);
			ignore_regex=string_copy(arg);
		}
		else if(string_eq_cstr(arg,"--only"))
		{
			string_free(arg);
			arg=(string_t*)array_shift(args);
			only_columns=string_split(arg,",",NULL);
		}
		else if(string_eq_cstr(arg,"--only-regex"))
		{
			string_free(arg);
			arg=(string_t*)array_shift(args);
			only_regex=string_copy(arg);
		}
		else if(string_eq_cstr(arg,"--drop"))
		{
			string_free(arg);
			arg=(string_t*)array_shift(args);
			drop_regex=string_copy(arg);
		}
		else if(string_eq_cstr(arg,"--version"))
		{
			print_version(argv[0]);
			exit(0);
		}
		else if(string_eq_cstr(arg,"--no-unk-ngrams"))
		{
			no_unk_ngrams=1;
		}
		else if(string_eq_cstr(arg,"--anti-prior"))
		{
			enforce_anti_priors=1;
		}
		/*else if(string_eq_cstr(arg,"--display-maxclass"))
		{
			display_maxclass_error=1;
		}*/
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
		else if(string_eq_cstr(arg,"--dev"))
		{
			string_free(arg);
			arg=(string_t*)array_shift(args);
			dev_filename=string_copy(arg);
		}
		else if(string_eq_cstr(arg,"--test"))
		{
			string_free(arg);
			arg=(string_t*)array_shift(args);
			test_filename=string_copy(arg);
		}
		else if(string_eq_cstr(arg,"--pack-model"))
		{
			pack_model=1;
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
		else if(string_eq_cstr(arg,"--optimal-iterations"))
		{
			optimal_iterations=1;
		}
		else if(string_eq_cstr(arg,"--max-fmeasure"))
		{
			string_free(arg);
			arg = array_shift(args);
			if(arg == NULL) die("ERROR: f-measure class not specified");
			fmeasure_class = string_copy(arg);
			use_max_fmeasure = 1;
		}
		else if(string_eq_cstr(arg,"--fmeasure-beta"))
		{
			string_free(arg);
			arg = array_shift(args);
			fmeasure_beta = string_to_double(arg);
			if(isnan(fmeasure_beta)) die("f-measure beta \"%s\" is not valid", arg->data);
		}
		else if(string_eq_cstr(arg,"-N"))
		{
			string_free(arg);
			arg=array_shift(args);
			if(arg==NULL)
			{
				die("not enough parameters for argument \"-N\"");
			}
			else if(string_eq_cstr(arg,"sgram"))
				text_expert_type=TEXT_EXPERT_SGRAM;
			else if(string_eq_cstr(arg,"fgram"))
				text_expert_type=TEXT_EXPERT_FGRAM;
			else if(string_eq_cstr(arg,"ngram"))
				text_expert_type=TEXT_EXPERT_NGRAM;
			else
			{
				die("invalid text expert type \"%s\"", arg->data);
			}
		}
		else if(string_eq_cstr(arg, "-W"))
		{
			string_free(arg);
			arg=array_shift(args);
			text_expert_length=string_to_int32(arg);
			if(!text_expert_length>0)
			{
				die("invalid window length \"%s\"",arg->data);
			}
		}
		else if(string_eq_cstr(arg, "--sequence"))
		{
			sequence_classification = 1;
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
	if(names_filename==NULL)
	{
		names_filename = string_copy(stem);
		string_append_cstr(names_filename, ".names");
	}
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
            string_t* options = NULL;
            if(parts->length > 2) {
                options = (string_t*)array_get(parts, 2);
            }
			template->dictionary = hashtable_new();
			template->tokens = vector_new(16);
			template->values = vector_new_float(16);
			template->classifiers = NULL;
            template->text_expert_type = text_expert_type;
            template->text_expert_length = text_expert_length;
            template->drop_regex = drop_regex;
            template->no_unk_ngrams = no_unk_ngrams;
            template->feature_count_cutoff = feature_count_cutoff;
            if(options != NULL) {
                array_t* option_list = string_split(options, "[ \t]+", NULL);
                int option_num;
                for(option_num = 0; option_num < option_list->length; option_num ++) {
                    string_t* option = (string_t*) array_get(option_list, option_num);
                    array_t* option_parts = string_split(option, "=", NULL);
                    string_t* option_name = (string_t*) array_get(option_parts, 0);
                    string_t* option_value = (string_t*) array_get(option_parts, 1);
                    if(verbose)fprintf(stderr, "OPTION(%s): %s %s\n", template->name->data, option_name->data, option_value->data);
                    if(string_eq_cstr(option_name, "expert_type")) {
                        if(string_eq_cstr(option_value,"sgram"))
                            template->text_expert_type=TEXT_EXPERT_SGRAM;
                        else if(string_eq_cstr(option_value,"fgram"))
                            template->text_expert_type=TEXT_EXPERT_FGRAM;
                        else if(string_eq_cstr(option_value,"ngram"))
                            template->text_expert_type=TEXT_EXPERT_NGRAM;
                        else {
                            die("unknown expert type \"%s\", line %d in %s", option->data, line_num+1, names_filename->data);
                        }
                    } else if(string_eq_cstr(option_name, "drop")) {
                        template->drop_regex = string_copy(option_value);
                        if(verbose) fprintf(stderr, "%s\n", template->drop_regex->data);
                    } else if(string_eq_cstr(option_name, "no_unk")) {
			            template->no_unk_ngrams=string_to_int32(option_value);
                        if(template->no_unk_ngrams != 0 && template->no_unk_ngrams != 1)
                        {
                            die("invalid value for no_unk \"%s\", line %d in %s", option->data, line_num+1, names_filename->data);
                        }
                    } else if(string_eq_cstr(option_name, "expert_length")) {
			            template->text_expert_length=string_to_int32(option_value);
                        if(!template->text_expert_length>0)
                        {
                            die("invalid expert length \"%s\", line %d in %s", option->data, line_num+1, names_filename->data);
                        }
                    } else if(string_eq_cstr(option_name, "cutoff")) {
			            template->feature_count_cutoff=string_to_int32(option_value);
            			if(template->feature_count_cutoff<=0) die("invalid cutoff \"%s\", line %d in %s", option->data, line_num+1, names_filename->data);
                    } else {
                        die("unknown option \"%s\", line %d in %s", option->data, line_num+1, names_filename->data);
                    }
                    string_array_free(option_parts);
                }
                string_array_free(option_list);
                string_free(options);
            }
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
			else if(!strcmp(type->data, "scored text")) {
                template->type = FEATURE_TYPE_IGNORE;
                warn("ignoring column \"%s\" of type \"%s\", line %d in %s", template->name->data, type->data, line_num+1, names_filename->data);
            }
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
			if(verbose)fprintf(stdout,"TEMPLATE: %d %s %d\n",template->column,template->name->data,template->type);
		}
		else // first line contains the class definitions
		{
			array_t* parts = string_split(line, "(^ +| *, *| *\\.$)", NULL);
			if(parts->length <= 1)die("invalid classes definition \"%s\", line %d in %s", line->data, line_num+1, names_filename->data);
			classes = vector_from_array(parts);
			array_free(parts);
			if(verbose)
			{
				fprintf(stdout,"CLASSES:");
				for(i=0;i<classes->length;i++)fprintf(stdout," %s",((string_t*)vector_get(classes,i))->data);
				fprintf(stdout,"\n");
			}
			if(use_max_fmeasure)
			{
				fmeasure_class_id = -1;
				for(i=0;i<classes->length;i++)
				{
					string_t* class = vector_get(classes, i);
					if(string_eq(class, fmeasure_class)) fmeasure_class_id = i;
				}
				if(fmeasure_class_id == -1) die("invalid fmeasure class \"%s\"", fmeasure_class->data);
			}
		}
		string_free(line);
		line_num++;
	}
	if(ignore_columns!=NULL)
	{
        int ignored = 0;
		for(i=0; i<ignore_columns->length; i++)
		{
			string_t* column=(string_t*)array_get(ignore_columns, i);
			template_t* template=hashtable_exists(templates_by_name, column->data, column->length);
			if(template!=NULL)
			{
                ignored ++;
				template->type=FEATURE_TYPE_IGNORE;
				if(verbose>0) { warn("ignoring column \"%s\"", column->data); }
			}
		}
		string_array_free(ignore_columns);
        if(ignored == 0) warn("no column ignored");
	}
	if(ignore_regex!=NULL)
	{
        int ignored = 0;
		for(i=0; i<templates->length; i++)
		{
			template_t* template=vector_get(templates, i);
			if(string_match(template->name, ignore_regex->data, "n"))
			{
                ignored ++;
				template->type=FEATURE_TYPE_IGNORE;
				if(verbose>0) { warn("ignoring column \"%s\"", template->name->data); }
			}
		}
		string_free(ignore_regex);
        if(ignored == 0) warn("no column ignored");
	}
	if(only_columns!=NULL)
	{
		for(i=0; i<templates->length; i++)
		{
			int j;
			int will_be_ignored = 1;
			template_t* template=vector_get(templates, i);
			for(j=0; j<only_columns->length; j++)
			{
				string_t* column=(string_t*)array_get(only_columns, j);
				if(string_eq(column, template->name))
				{
					will_be_ignored = 0;
					break;
				}
			}
			if(will_be_ignored)
			{
				template->type=FEATURE_TYPE_IGNORE;
				if(verbose>0) { warn("ignoring column \"%s\"", template->name->data); }
			}
		}
		string_array_free(only_columns);
	}
	if(only_regex!=NULL)
	{
		for(i=0; i<templates->length; i++)
		{
			template_t* template=vector_get(templates, i);
			if(! string_match(template->name, only_regex->data, "n"))
			{
				template->type=FEATURE_TYPE_IGNORE;
				if(verbose>0) { warn("ignoring column \"%s\"", template->name->data); }
			}
		}
		string_free(only_regex);
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
		classifiers = load_model(templates,classes,model_name->data, test_time_iterations);
		//fprintf(stderr, "test_time_iterations: %d\n", test_time_iterations);
		if(test_time_iterations > classifiers->length || test_time_iterations == -1) test_time_iterations = classifiers->length;
		double sum_of_alpha=0;
		int errors=0;
		int errors_argmax = 0;
		//int by_class_errors[classes->length];
		//int by_class_errors_argmax[classes->length];
		int by_class_totals[classes->length];
		int by_class_totals_argmax[classes->length];
		int by_class_pred[classes->length];
		int by_class_pred_argmax[classes->length];
		int by_class_correct[classes->length];
		int by_class_correct_argmax[classes->length];
		//memset(by_class_errors, 0, sizeof(int) * classes->length);
		memset(by_class_totals, 0, sizeof(int) * classes->length);
		memset(by_class_pred, 0, sizeof(int) * classes->length);
		memset(by_class_correct, 0, sizeof(int) * classes->length);
		//memset(by_class_errors_argmax, 0, sizeof(int) * classes->length);
		memset(by_class_totals_argmax, 0, sizeof(int) * classes->length);
		memset(by_class_pred_argmax, 0, sizeof(int) * classes->length);
		memset(by_class_correct_argmax, 0, sizeof(int) * classes->length);
		
		int num_examples=0;
		for(i=0;i<classifiers->length;i++)
		{
			weakclassifier_t* classifier=vector_get(classifiers,i);
			sum_of_alpha+=classifier->alpha;
		}
		string_free(model_name);

		string_t* line=NULL;
		int line_num=0;
		string_t* previous_decision = string_new("?");
		double decision_threshold = 0.0;
		if(output_posteriors)
		{
			decision_threshold = 0.5;
		}
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
				die("wrong number of columns (%zd), \"%s\", line %d in %s", array_of_tokens->length, line->data, line_num, "stdin");
			double score[classes->length];
			for(l=0; l<classes->length; l++) score[l]=0.0;
			for(i=0; i<templates->length; i++)
			{
				template_t* template=vector_get(templates, i);
				string_t* token=array_get(array_of_tokens, i);
				if(sequence_classification == 1 && string_eq_cstr(template->name, "__SEQUENCE_PREVIOUS"))
				{
					token = previous_decision;
				}
				if(template->type == FEATURE_TYPE_TEXT || template->type == FEATURE_TYPE_SET)
				{
					hashtable_t* subtokens=hashtable_new();
					if(string_cmp_cstr(token,"?")!=0)
					{
						int j;
						array_t* experts=text_expert(template, token);
                        if(template->type == FEATURE_TYPE_SET && experts->length != 1) {
                            die("value \"%s\" was not described in the .names file, line %d, column %d (%s) in %s", token->data, line_num, i, template->name->data, "stdin");
                        }
                        for(j=0; j<experts->length; j++)
                        {
                            string_t* expert=array_get(experts, j);
                            tokeninfo_t* tokeninfo=hashtable_get(template->dictionary, expert->data, expert->length);
                            int id = -1;
                            if(tokeninfo!=NULL) {
                                hashtable_set(subtokens, &tokeninfo->id, sizeof(tokeninfo->id), tokeninfo);
                                id = tokeninfo->id;
                            } else if(template->type == FEATURE_TYPE_SET) {
                                die("token \"%s\" was not defined in names file, line %d in %s", expert->data, line_num, "stdin");
                            }
                            if(verbose) fprintf(stderr, "  EXPERT(%s): \"%s\" (id=%d)\n", template->name->data, expert->data, id);
                        }
                        string_array_free(experts);
                    }
                    int j;
                    for(j=0; j<template->classifiers->length; j++)
                    {
                        weakclassifier_t* classifier=vector_get(template->classifiers, j);
                        if(hashtable_get(subtokens, &classifier->token, sizeof(classifier->token))==NULL) {
                            if(verbose) {
                                fprintf(stderr, "    WEAKC %s=%d: C1 (absent)", classifier->template->name->data, classifier->token);
                                for(l=0; l<classes->length; l++) {
                                    fprintf(stderr, " %g", classifier->alpha*classifier->c1[l]);
                                }
                                fprintf(stderr, "\n");
                            }
                            for(l=0; l<classes->length; l++) score[l]+=classifier->alpha*classifier->c1[l];
                        } else {
                            if(verbose) {
                                fprintf(stderr, "    WEAKC %s=%d: C2 (present)", classifier->template->name->data, classifier->token);
                                for(l=0; l<classes->length; l++) {
                                    fprintf(stderr, " %g", classifier->alpha*classifier->c2[l]);
                                }
                                fprintf(stderr, "\n");
                            }
                            for(l=0; l<classes->length; l++) score[l]+=classifier->alpha*classifier->c2[l];
                        }
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
                        if(isnan(value)) {
                            if(verbose) {
                                fprintf(stderr, "    WEAKC %s=nan: C0 (unk)", classifier->template->name->data);
                                for(l=0; l<classes->length; l++) {
                                    fprintf(stderr, " %g", classifier->alpha*classifier->c1[l]);
                                }
                                fprintf(stderr, "\n");
                            }
                            for(l=0; l<classes->length; l++) score[l]+=classifier->alpha*classifier->c0[l];
                        } else if(value < classifier->threshold) {
                            if(verbose) {
                                fprintf(stderr, "    WEAKC %s<%f: C1 (<)", classifier->template->name->data, classifier->threshold);
                                for(l=0; l<classes->length; l++) {
                                    fprintf(stderr, " %g", classifier->alpha*classifier->c1[l]);
                                }
                                fprintf(stderr, "\n");
                            }
                            for(l=0; l<classes->length; l++) score[l]+=classifier->alpha*classifier->c1[l];
                        } else {
                            if(verbose) {
                                fprintf(stderr, "    WEAKC %s>=%f: C2 (>=)", classifier->template->name->data, classifier->threshold);
                                for(l=0; l<classes->length; l++) {
                                    fprintf(stderr, " %g", classifier->alpha*classifier->c2[l]);
                                }
                                fprintf(stderr, "\n");
                            }
                            for(l=0; l<classes->length; l++) score[l]+=classifier->alpha*classifier->c2[l];
                        }
                    }
                }
                // FEATURE_TYPE_IGNORE
            }
            /*string_t* true_class=NULL;
              vector_t* tokens=array_to_vector(array_of_tokens);
              if(tokens->length > templates->length)
              {
              true_class=vector_get(tokens, tokens->length-1);
              while(true_class->data[true_class->length-1]=='.')
              {
              true_class->data[true_class->length-1]='\0';
              true_class->length--;
              }
              }*/
            int example_classes[classes->length];
            memset(example_classes, 0, sizeof(int) * classes->length);
            if(array_of_tokens->length > templates->length)
            {
                string_t* last_token = array_get(array_of_tokens, templates->length);
                array_t* array_of_labels = string_split(last_token, "( *\\.$|  *)", NULL);
                int j;
                for(j=0; j<array_of_labels->length; j++)
                {
                    string_t* class = array_get(array_of_labels, j);
                    for(l=0; l<classes->length; l++)
                    {
                        string_t* other = vector_get(classes, l);
                        if(string_eq(other, class))
                        {
                            example_classes[l] = 1;
                            break;
                        }
                    }
                    if(l == classes->length) die("unknown class label \"%s\", line %d in %s", class->data, line_num, "stdin");
                }
                string_array_free(array_of_labels);
            }
            for(l=0; l<classes->length; l++)
            {
                score[l]/=sum_of_alpha;
                if(output_posteriors)
                {
                    score[l] = 1.0/(1.0+exp(-2*sum_of_alpha*score[l]));
                    score[l]-=decision_threshold;
                }
            }
            if(!dryrun_mode)
            {
                if(sequence_classification == 1)
                {
                    array_t* predicted_labels = array_new();
                    for(l=0; l<classes->length; l++)
                    {
                        if(score[l]>0) array_push(predicted_labels, vector_get(classes, l));
                    }
                    string_free(previous_decision);
                    previous_decision = string_join_cstr(" ", predicted_labels);
                    fprintf( stdout, "previous_decision = [%s]\n", previous_decision->data);
                    array_free(predicted_labels);
                }
                if(classification_output==0)
                {
                    for(l=0; l<classes->length; l++)
                    {
                        fprintf(stdout, "%d ", example_classes[l]);
                    }
                    //fprintf(stdout," scores:");
                    int erroneous_example = 0;
					int erroneous_example_argmax = 0;
					double max = 0.0;
					int max_class = -10000;
                    for(l=0; l<classes->length; l++)
                    {
                        fprintf(stdout,"%.12f",score[l]+decision_threshold);
                        if(l<classes->length-1)fprintf(stdout," ");
                        if((score[l]<=0 && example_classes[l]!=0))erroneous_example = 1;
                        if((score[l]>0 && example_classes[l]==0))erroneous_example = 1;
						if (score[l]>max) {
							max = score[l];
							max_class = l;
						}
                        //fprintf(stdout, " %f", score[l]);
                    }
                    //fprintf(stdout, " %s\n", erroneous_example ? "ERROR" : "OK" );
                    if(erroneous_example == 1) errors++;
					
					if (example_classes[max_class]==0) {
						erroneous_example_argmax = 1;
					}
					
					if (erroneous_example_argmax == 1) {
						errors_argmax++;
					}
					
                    fprintf(stdout,"\n");
                }
                else
                {
					int erroneous_example_argmax = 0;
					
                    fprintf(stdout,"\n\n");
                    for(i=0; i<templates->length; i++)
                    {
                        template_t* template=vector_get(templates,i);
                        string_t* token=array_get(array_of_tokens,i);
                        fprintf(stdout,"%s: %s\n", template->name->data, token->data);
                    }
                    /*if(true_class!=NULL)
                      {*/
                    fprintf(stdout,"correct label = ");
                    for(l=0; l<classes->length; l++)
                    {
                        if(example_classes[l]==1)
                        {
                            string_t* class = vector_get(classes, l);
                            fprintf(stdout,"%s ", class->data);
                        }
                    }
                    fprintf(stdout,"\n");
                    int erroneous_example = 0;
                    if(display_maxclass_error) {
                        double max = 0;
                        int argmax = 0;
                        int label = 0;
                        for(l=0; l<classes->length; l++) {
                            if(l == 0 || score[l] > max) {
                                max = score[l];
                                argmax = l;
                            }
                        }
                        for(l=0; l<classes->length; l++) {
                            string_t* class=vector_get(classes,l);
                            fprintf(stdout,"%s%s % 5f : %s \n", example_classes[l]!=0?"*":" ",
                                    (l == argmax?">":(example_classes[l]!=0?"*":" ")),score[l]+decision_threshold,class->data);
                            if(example_classes[l] != 0) {
                                by_class_totals[l]++;
                                label = l;
                            }
                        }
                        if(label != argmax) {
                            erroneous_example = 1;
                            //by_class_errors[label] += 1;
                        }
						else {
							by_class_correct_argmax[argmax]++;
						}
						by_class_pred_argmax[argmax]++;
						
                    } else {
						
						double max = 0;
						int argmax = 0;
						int label = 0;
						
                        for(l=0; l<classes->length; l++)
                        {
							if (l == 0 || score[l] > max) {
								max = score[l];
								argmax = l;
							}
                            string_t* class=vector_get(classes,l);
                            fprintf(stdout,"%s%s % 5f : %s \n", example_classes[l]!=0?"*":" ",
                                    (score[l]>0?">":(example_classes[l]!=0?"*":" ")),score[l]+decision_threshold,class->data);
							
							if (example_classes[l]!=0) {
								by_class_totals_argmax[l]++;
								label = l;
							}
							
                            if((!(score[l]>0) && example_classes[l]!=0))
                            {
								erroneous_example=1;
								//by_class_errors[l]++;
                            }
                            else if((score[l]>0 && example_classes[l]==0))
                            {
								erroneous_example=1;
								//by_class_errors[l]++;
                            }
							
							if (score[l]>0) {
								by_class_pred[l]++;
							}
							if (score[l]>0 && example_classes[l]!=0) {
								by_class_correct[l]++;
							}
							
                            if(example_classes[l]!=0) {
                                by_class_totals[l]++;
                            }
                        }
						if (label != argmax) {
							erroneous_example_argmax = 1;
							//by_class_errors_argmax[label]++;
						}
						else {
							by_class_correct_argmax[argmax]++;
						}
						by_class_pred_argmax[argmax]++;

                    }
                    if(erroneous_example == 1) errors++;
					if (erroneous_example_argmax == 1) {
						errors_argmax++;
					}
                    /*}
                      else
                      {
                      fprintf(stdout,"correct label = ?\n");
                      for(l=0; l<classes->length; l++)
                      {
                      string_t* class=vector_get(classes,l);
                      fprintf(stdout,"   % 5f : %s\n", score[l],class->data);
                      }
                      }*/
                }
            }
            string_array_free(array_of_tokens);
            string_free(line);
            num_examples++;
        }
        if(!dryrun_mode && classification_output!=0)
        {
            fprintf(stderr,"EXAMPLE ERROR RATE (>0): %f (%d/%d)\n",(double)errors/(double)(num_examples), errors, num_examples);
            int l;
            for(l=0; l<classes->length; l++)
            {
                string_t* class = vector_get(classes, l);
				double precision, recall, f_measure;
				
				if (by_class_pred[l]!=0) {
					precision = ((double)by_class_correct[l])/((double)by_class_pred[l]);
				}
				
				if (by_class_totals[l]!=0) {
					recall = ((double)by_class_correct[l])/((double)by_class_totals[l]);
				}
				else {
					recall = 0;
				}

				if (recall!=0) {
					f_measure = (2*precision*recall)/(precision+recall);
				}
				
                fprintf(stderr, "  %s: r=%f p=%f f=%f (tp=%d, fp=%d, pp=%d)\n", class->data,
                        (by_class_totals[l]==0 ? NAN : recall),
						(by_class_pred[l]==0 ? NAN : precision),
						(recall==0 ? NAN : f_measure),
						by_class_correct[l], by_class_pred[l]-by_class_correct[l], by_class_totals[l]);
            }
			fprintf(stderr,"EXAMPLE ERROR RATE (argmax): %f (%d/%d)\n",(double)errors_argmax/(double)(num_examples), errors_argmax, num_examples);
            for(l=0; l<classes->length; l++)
            {
                string_t* class = vector_get(classes, l);
				double precision, recall, f_measure;
				
				if (by_class_pred_argmax[l]!=0) {
					precision = ((double)by_class_correct_argmax[l])/((double)by_class_pred_argmax[l]);
				}
				
				if (by_class_totals_argmax[l]!=0) {
					recall = ((double)by_class_correct_argmax[l])/((double)by_class_totals_argmax[l]);
				}
				else {
					recall = 0;
				}
				
				if (recall!=0) {
					f_measure = (2*precision*recall)/(precision+recall);
				}
				
                fprintf(stderr, "  %s: r=%f p=%f f=%f (tp=%d, fp=%d, pp=%d)\n", class->data,
                        (by_class_totals_argmax[l]==0 ? NAN : recall),
						(by_class_pred_argmax[l]==0 ? NAN : precision),
						(recall==0 ? NAN : f_measure),
						by_class_correct_argmax[l], by_class_pred_argmax[l]-by_class_correct_argmax[l], by_class_totals_argmax[l]);
            }
        }
        string_free(previous_decision);
        exit(0);
    }

	double class_priors[classes->length];
	// load a train, dev and test sets (if available)
	if(data_filename==NULL)
	{
		data_filename = string_copy(stem);
		string_append_cstr(data_filename, ".data");
	}
	vector_t* examples = load_examples_multilabel(data_filename->data, templates, classes, class_priors, feature_count_cutoff, 0);
	if(examples == NULL || examples->length == 0) {
		die("no training examples found in \"%s\"", data_filename->data);
	}
	string_free(data_filename);

	// generate a simple random sequence of example ids for sampling
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

// deactivated dev/test that don't work with the new indexed features (need separate handling)

	if(dev_filename == NULL) {
		dev_filename = string_copy(stem);
		string_append_cstr(dev_filename, ".dev");
	}
	dev_examples = load_examples_multilabel(dev_filename->data, templates, classes, NULL, 0, 1);
	if(dev_examples != NULL && dev_examples->length == 0) {
		warn("no dev examples found in \"%s\"", dev_filename->data);
		vector_free(dev_examples);
		dev_examples = NULL;
	}
	string_free(dev_filename);

	if(test_filename == NULL) {
		test_filename = string_copy(stem);
		string_append_cstr(test_filename, ".test");
	}
	test_examples = load_examples_multilabel(test_filename->data, templates, classes, NULL, 0, 1);
	if(test_examples != NULL && test_examples->length == 0) {
		warn("no test examples found in \"%s\"", test_filename->data);
		vector_free(test_examples);
		test_examples = NULL;
	}
	string_free(test_filename);

	if(model_name==NULL)
	{
		model_name = string_copy(stem);
		string_append_cstr(model_name, ".shyp");
	}

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

    if(has_multiple_labels_per_example) {
        display_maxclass_error = 0;
    } else {
        display_maxclass_error = 1;
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
	//toolbox->result_available=sem_open("result_available",O_CREAT,0700,0);
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
	double theoretical_error=1.0;
	double minimum_test_error=1.0;
	double optimal_iteration_threshold = NAN;
	if(use_max_fmeasure) minimum_test_error = 0.0;
	vector_t* classifiers = NULL;
	if(resume_training == 0)
	{
		classifiers = vector_new(maximum_iterations);
	}
	else
	{
		classifiers=load_model(templates, classes, model_name->data, -1);
		for(; iteration<classifiers->length; iteration++)
		{
			weakclassifier_t* classifier = vector_get(classifiers, iteration);
			double error=compute_classification_error(classifiers, examples, iteration, sum_of_weights, classes->length); // compute error rate and update weights
			if(use_max_fmeasure) error = compute_max_fmeasure(examples, fmeasure_class_id, NULL, NULL, NULL);
            else if(display_maxclass_error) error = compute_max_error(examples, classes->length);
			double dev_error=NAN;
			//double dev_error_monoclass=NAN;
			double threshold = NAN;
			if(dev_examples!=NULL)
			{
				dev_error = compute_test_error(classifiers, dev_examples, iteration, classes->length); // compute error rate on test
				if(use_max_fmeasure) dev_error = compute_max_fmeasure(dev_examples, fmeasure_class_id, &threshold, NULL, NULL);
                else if(display_maxclass_error) dev_error= compute_max_error(dev_examples, classes->length);
				if(optimal_iterations!=0 && ((!use_max_fmeasure && dev_error<minimum_test_error) || (use_max_fmeasure && dev_error>minimum_test_error)))
				{
					minimum_test_error=dev_error;
					optimal_iterations=iteration+1;
					optimal_iteration_threshold = threshold;
				}
			}
			double test_error=NAN;
			//double test_error_monoclass=NAN;
			if(test_examples!=NULL)
			{
				test_error = compute_test_error(classifiers, test_examples, iteration, classes->length); // compute error rate on test
				if(use_max_fmeasure) test_error = compute_max_fmeasure(test_examples, fmeasure_class_id, &threshold, NULL, NULL);
                else if (display_maxclass_error) test_error = compute_max_error(test_examples, classes->length);
			}
			if(dev_examples==NULL && test_examples!=NULL)
			{
				if(optimal_iterations!=0 && ((!use_max_fmeasure && test_error<minimum_test_error) || (use_max_fmeasure && test_error>minimum_test_error)))
				{
					minimum_test_error=test_error;
					optimal_iterations=iteration+1;
					optimal_iteration_threshold = threshold;
				}
			}
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
			//theoretical_error*=classifier->objective;
			theoretical_error=NAN;
			//if (display_maxclass_error) fprintf(stdout,"rnd %d: wh-err= %f th-err= %f dev= %f test= %f train= %f mc-dev= %f mc-test= %f\n",iteration+1,classifier->objective,theoretical_error,dev_error,test_error,error,dev_error_monoclass,test_error_monoclass);
			fprintf(stdout,"rnd %d: wh-err= %f th-err= %f dev= %f test= %f train= %f\n",iteration+1,classifier->objective,theoretical_error,dev_error,test_error,error);
		}
	}
	for(;iteration<maximum_iterations;iteration++)
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
		for(i=0;i<templates->length;i++) // find the best classifier
		{
			template_t* template=(template_t*)vector_get(templates,i);
			weakclassifier_t* current=NULL;
			if(template->type==FEATURE_TYPE_CONTINUOUS)
			{
				if(use_known_continuous_stump)
					current=train_known_continuous_stump(1.0, template, examples, classes->length);
				else
					current=train_continuous_stump(1.0, template, examples, classes->length);
			}
			else if(template->type==FEATURE_TYPE_TEXT || template->type==FEATURE_TYPE_SET)
			{
				if(use_abstaining_text_stump)
					current=train_abstaining_text_stump(1.0, template, examples, sum_of_weights, classes->length);
				else
					current=train_text_stump(1.0, template, examples, sum_of_weights, classes->length);
			}
			// else => FEATURE_TYPE_IGNORE
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
		//if(iteration==maximum_iterations-1) output_scores=1; else output_scores=0;
		double error=compute_classification_error(classifiers, examples, iteration, sum_of_weights, classes->length); // compute error rate and update weights
		double train_recall = NAN;
		double train_precision = NAN;
		if(use_max_fmeasure) error = compute_max_fmeasure(examples, fmeasure_class_id, NULL, &train_recall, &train_precision);
        else if(display_maxclass_error) error = compute_max_error(examples, classes->length);
		double dev_recall = NAN;
		double dev_precision = NAN;
		double dev_error=NAN;
		//double dev_error_monoclass=NAN;
		double threshold = NAN;
		if(dev_examples!=NULL)
		{
			dev_error = compute_test_error(classifiers, dev_examples, iteration, classes->length); // compute error rate on test
			if(use_max_fmeasure) dev_error = compute_max_fmeasure(dev_examples, fmeasure_class_id, &threshold, &dev_recall, &dev_precision);
            else if (display_maxclass_error) dev_error = compute_max_error(dev_examples, classes->length);
			if(optimal_iterations!=0 && ((!use_max_fmeasure && dev_error<minimum_test_error) || (use_max_fmeasure && dev_error>minimum_test_error)))
			{
				minimum_test_error=dev_error;
				optimal_iterations=iteration+1;
				optimal_iteration_threshold = threshold;
			}
		}
		double test_error=NAN;
		//double test_error_monoclass=NAN;
		double test_recall = NAN;
		double test_precision = NAN;
		double test_threshold = NAN;
		if(test_examples!=NULL)
		{
			test_error = compute_test_error(classifiers, test_examples, iteration, classes->length); // compute error rate on test
			if(use_max_fmeasure) test_error = compute_max_fmeasure(test_examples, fmeasure_class_id, &test_threshold, &test_recall, &test_precision);
            else if (display_maxclass_error) test_error = compute_max_error(test_examples, classes->length);
		}
		if(dev_examples==NULL && test_examples!=NULL)
		{
			if(optimal_iterations!=0 && ((!use_max_fmeasure && test_error<minimum_test_error) || (use_max_fmeasure && test_error>minimum_test_error)))
			{
				minimum_test_error=test_error;
				optimal_iterations=iteration+1;
				optimal_iteration_threshold = test_threshold;
			}
		}
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
		theoretical_error*=classifier->objective;
		/*if (display_maxclass_error)
		{
			if(use_max_fmeasure) fprintf(stdout,"rnd %d: wh-err= %f th-err= %f dev= %f (R=%.3f, P=%.3f) test= %f (R=%.3f, P=%.3f) train= %f (R=%.3f, P=%.3f) mc-dev= %f mc-test= %f\n",iteration+1,classifier->objective,theoretical_error,dev_error,dev_recall, dev_precision, test_error, test_recall, test_precision, error, train_recall, train_precision,dev_error_monoclass,test_error_monoclass);
			else fprintf(stdout,"rnd %d: wh-err= %f th-err= %f dev= %f test= %f train= %f mc-dev= %f mc-test= %f\n",iteration+1,classifier->objective,theoretical_error,dev_error,test_error,error,dev_error_monoclass,test_error_monoclass);
		}
		else*/
		{
			if(use_max_fmeasure) fprintf(stdout,"rnd %d: wh-err= %f th-err= %f dev= %f (R=%.3f, P=%.3f) test= %f (R=%.3f, P=%.3f) train= %f (R=%.3f, P=%.3f)\n",iteration+1,classifier->objective,theoretical_error,dev_error,dev_recall, dev_precision, test_error, test_recall, test_precision, error, train_recall, train_precision);
			else fprintf(stdout,"rnd %d: wh-err= %f th-err= %f dev= %f test= %f train= %f\n",iteration+1,classifier->objective,theoretical_error,dev_error,test_error,error);
		}
		if(save_model_at_each_iteration) save_model(classifiers, classes, model_name->data, 0, 0);
        fflush(stdout);
	}

	save_model(classifiers, classes, model_name->data,pack_model, optimal_iterations);
	if(optimal_iterations!=0)
	{
		fprintf(stdout,"OPTIMAL ITERATIONS: %d, %s = %f", optimal_iterations, use_max_fmeasure ? "max_fmeasure" : "error", minimum_test_error);
		optimal_iteration_threshold /= optimal_iterations;
		if(output_posteriors) optimal_iteration_threshold = 1.0 / (1.0 + exp(-2.0 * optimal_iterations * optimal_iteration_threshold));
		if(use_max_fmeasure) fprintf(stdout, ", threshold = %f\n", optimal_iteration_threshold);
		else fprintf(stdout, "\n");
	}
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
	if(dev_examples!=NULL)
	{
		for(i=0; i<dev_examples->length; i++)
		{
			int j;
			test_example_t* example=vector_get(dev_examples,i);
			FREE(example->continuous_features);
			for(j=0; j<templates->length ;j ++)
				if(example->discrete_features[j] != NULL)
					vector_free(example->discrete_features[j]);
			FREE(example->discrete_features);
			FREE(example->score);
			FREE(example);
		}
		vector_free(dev_examples);
	}
	if(test_examples!=NULL)
	{
		for(i=0; i<test_examples->length; i++)
		{
			int j;
			test_example_t* example=vector_get(test_examples,i);
			FREE(example->continuous_features);
			for(j=0; j<templates->length ;j ++)
				if(example->discrete_features[j] != NULL)
					vector_free(example->discrete_features[j]);
			FREE(example->discrete_features);
			FREE(example->score);
			FREE(example->classes);
			FREE(example);
		}
		vector_free(test_examples);
	}
	for(i=0; i<examples->length; i++)
	{
		example_t* example=(example_t*)vector_get(examples,i);
		FREE(example->weight);
		FREE(example->score);
		FREE(example->classes);
		FREE(example);
	}
	vector_free(examples);
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
