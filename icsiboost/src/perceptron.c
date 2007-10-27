#include "utils/common.h"
#include "utils/string.h"
#include "utils/vector.h"
#include "utils/array.h"
#include "examples.h"

vector_implement_functions_for_type(float, 0.0);

int main(int argc, char** argv)
{
	vector_t* examples=vector_new(16);
	vector_t* weights=vector_new_float(16);
	string_t* line=NULL;
	while((line=string_readline(stdin)))
	{
		example_t* example=example_new(line);
		vector_push(examples, example);
		if(example->label!=1.0){example->label=0.0;}
	}

	int iteration;
	double alpha=0.001;
	int i,j;
	double error=0;
	for(iteration=0; iteration<10; iteration++)
	{
		error=0;
		for(i=0; i<examples->length; i++)
		{
			example_t* example=vector_get(examples, i);
			double prediction=0;
			for(j=0; j<example->features->length; j++)
			{
				feature_t* feature=vector_get(example->features, j);
				while(weights->length<=feature->id)
				{
					vector_push_float(weights, 0.0);
				}
				float weight=vector_get_float(weights, feature->id);
				prediction+=weight*feature->value;
			}
			if(prediction>0.0)prediction=1.0;
			else prediction=0.0;
			if(prediction!=example->label)error++;
			vector_set_float(weights, 0, vector_get_float(weights, 0)+alpha*(example->label-prediction)*1.0);
			for(j=0; j<example->features->length; j++)
			{
				feature_t* feature=vector_get(example->features, j);
				vector_set_float(weights, feature->id, vector_get_float(weights, feature->id)+alpha*(example->label-prediction)*feature->value);
			}
		}
	}
	fprintf(stdout,"iteration %d, error %f\n", iteration, error/examples->length);
	
	/*int num=10000;
	int dims=100;
	int i,j;
	double vector[num][dims];
	double weight[dims];
	double label[num];
	for(i=0; i<dims; i++)weight[i]=0;
	for(i=0; i<num; i++)label[i]=(i%2);
	for(i=0; i<num; i++)
	{
		for(j=0; j<dims-1; j++)
		{
			vector[i][j] = ((double)rand()/RAND_MAX+i%2)*2.0-1.0*rand()/RAND_MAX;
		}
		vector[i][dims-1] = 1;
	}

	int iteration;
	double alpha=0.1;
	for(iteration=0; iteration<10; iteration++)
	{
		double error=0;
		for(i=0; i<num; i++)
		{
			double prediction=0;
			double sum_of_weights=0;
			for(j=0; j<dims; j++)
			{
				prediction+=weight[j]*vector[i][j];
				sum_of_weights+=weight[j];
			}
			if(prediction>0.0)prediction=1.0;
			else prediction=0.0;
			//fprintf(stdout,"%d %f %f %f\n",i,label[i],prediction,sum_of_weights);
			if(prediction!=label[i])error++;
			for(j=0; j<dims; j++)
			{
				weight[j]+=alpha*(label[i]-prediction)*vector[i][j];
			}
		}
		fprintf(stdout,"ERROR %d %f\n", iteration, error/num);
	}*/
}
