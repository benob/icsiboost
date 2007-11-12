#include "utils/common.h"
#include "utils/string.h"
#include "utils/vector.h"
#include "utils/array.h"
#include "examples.h"
#include <math.h>

vector_implement_functions_for_type(float, 0.0);

int main(int argc, char** argv)
{
	vector_t* examples=vector_new(16);
	vector_t* weights=vector_new_float(16);
	string_t* line=NULL;
	while((line=string_readline(stdin)))
	{
		if(string_match(line, "^#", "n"))continue;
		example_t* example=example_new(line);
		vector_push(examples, example);
	}

	int iteration;
	double alpha=1;
	int i,j;
	double error=0;
	double bias=0;
	for(iteration=0; iteration<10; iteration++)
	{
		error=0;
		for(i=0; i<examples->length; i++)
		{
			example_t* example=vector_get(examples, i);
			double prediction=bias;
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
			//fprintf(stdout,"  example %d, label=%f prediction=%f\n", i, example->label, prediction);
			if(prediction>0.0)prediction=1.0;
			else prediction=-1.0;
			if(prediction!=example->label)
			{
				error++;
				bias+=(example->label-prediction);
				for(j=0; j<example->features->length; j++)
				{
					feature_t* feature=vector_get(example->features, j);
					vector_set_float(weights, feature->id, vector_get_float(weights, feature->id)+alpha*(example->label-prediction)*feature->value);
				}
			}
		}
		fprintf(stdout,"iteration %d, error %f\n", iteration, error/examples->length);
	}
	return 0;
}
