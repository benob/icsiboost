#include "utils/common.h"
#include "utils/string.h"
#include "utils/vector.h"
#include "utils/array.h"

#include "examples.h"

example_t* example_new(string_t* line)
{
	string_chomp(line);
	example_t* example=MALLOC(sizeof(example_t));
	array_t* tokens=string_split(line, " ", NULL);
	string_t* current=array_shift(tokens);
	example->label=string_to_float(current);
	example->features=vector_new(tokens->length);
	string_free(current);
	while(tokens->length>0)
	{
		current=array_shift(tokens);
		//fprintf(stdout,"%d [%s]\n",example->features->length, current->data);
		array_t* parts=string_split(current, ":", NULL);
		feature_t* feature=MALLOC(sizeof(feature_t));
		if(parts==NULL || parts->length!=2)die("reading example \"%s\"", line->data);
		vector_push(example->features, feature);
		feature->id=string_to_int32( array_get(parts, 0));
		feature->value=string_to_float( array_get(parts, 1));
		array_free(parts);
		string_free(current);
	}
	array_free(tokens);
	vector_optimize(example->features);
	return example;
}

void example_free(example_t* example)
{
	vector_apply(example->features, vector_freedata, NULL);
	vector_free(example->features);
	FREE(example);
}

/*int main(int argc, char** argv)
{
	string_t* line=NULL;
	while((line=string_readline(stdin)))
	{
		example_t* example=load_example(line);
		example_free(example);
	}
	return 0;
}*/
