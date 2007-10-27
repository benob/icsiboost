#ifndef __EXAMPLES_H__
#define __EXAMPLES_H__

#include "utils/common.h"
#include "utils/string.h"
#include "utils/vector.h"
#include "utils/array.h"

typedef struct feature {
	int32_t id;
	float value;
} feature_t;

typedef struct example {
	vector_t* features;
	float label;
} example_t;

example_t* example_new(string_t* line);
void example_free(example_t* example);

#endif
