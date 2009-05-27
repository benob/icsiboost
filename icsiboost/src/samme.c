/* Copyright (C) (2007) (Benoit Favre) <favre@icsi.berkeley.edu>

This program is free software; you can redistribute it and/or 
modify it under the terms of the GNU Lesser General Public License 
as published by the Free Software Foundation; either 
version 2 of the License, or (at your option) any later 
version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU Lesser General Public License for more details.

You should have received a copy of the GNU Lesser General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA. */

/* this program implements SAMME as described in "Multi-class AdaBoost" by Ji
 * Zhu et al. (2005). Unlike icsiboost, it uses a different input format (no
 * .names), only supports discrete features, does not generate ngrams...
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "utils/hashtable.h"
#include "utils/vector.h"

vector_implement_functions_for_type(int32_t, 0);

typedef struct example {
    double weight;
    int label;
    double* label_score;
    int32_t id;
} example_t;

typedef struct feature {
    const char* text;
    vector_t* examples;
    int id;
} feature_t;

typedef struct label {
    const char* text;
    int id;
} label_t;

typedef struct classifier {
    double error;
    double alpha;
    feature_t* feature;
    int label;
} classifier_t;

int comparator(const void* a, const void* b) {
    if(a < b) return -1;
    if(a > b) return +1;
    return 0;
}

/* expected format:
 * one example per line, class label in the first token, features in subesquent tokens
 */
int read_examples(const char* input, vector_t* features, vector_t* examples, vector_t* labels) {
    hashtable_t* text_to_feature = hashtable_new();
    hashtable_t* text_to_label = hashtable_new();
    char word[1024];
    char* current = word;
    int line_num = 0;
    FILE* fp = fopen(input, "r");
    if(fp == NULL) die("reading input file %s", input);
    example_t* example = malloc(sizeof(example_t));
    if(example == NULL) die("unable to allocate memory for example");
    example->weight = 1.0;
    example->label = -1;
    example->id = 0;
    while(!feof(fp) && current - word < 1024) {
        *current = getc(fp);
        int end_of_line = 0;
        if(*current == '\n' || feof(fp)) end_of_line = 1;
        if(*current == ' ' || *current == '\t' || *current == '\n') {
            if(current != word) {
                int length = current - word;
                *current = '\0';
                if(example->label == -1) {
                    label_t* label = hashtable_get(text_to_label, word, length);
                    if(label == NULL) {
                        label = malloc(sizeof(label_t));
                        if(label == NULL) die("unable to allocate memory for label");
                        label->id = labels->length;
                        label->text = strdup(word);
                        hashtable_put(text_to_label, word, length, label);
                        vector_push(labels, label);
                    }
                    example->label = label->id;
                } else {
                    feature_t* feature = hashtable_get(text_to_feature, word, length);
                    if(feature == NULL) {
                        feature = malloc(sizeof(feature_t));
                        if(feature == NULL) die("unable to allocate memory for feature");
                        feature->id = features->length;
                        feature->text = strdup(word);
                        feature->examples = vector_new_int32_t(1);
                        hashtable_put(text_to_feature, word, length, feature);
                        vector_push(features, feature);
                    }
                    vector_push_int32_t(feature->examples, example->id);
                }
                current = word;
            }
        } else {
            current ++;
        }
        if(end_of_line) {
            vector_push(examples, example);
            example = malloc(sizeof(example_t));
            if(example == NULL) die("unable to allocate memory for example");
            example->weight = 1.0;
            example->label = -1;
            example->id = examples->length;
            if(examples->length % 1000 == 0) 
                fprintf(stderr, "\rexamples: %d, features: %d, labels: %d", 
                    (int)examples->length, (int)features->length, (int)labels->length);
        }
        line_num += 1;
    }
    fprintf(stderr, "\rexamples: %d, features: %d, labels: %d\n", 
            (int)examples->length, (int)features->length, (int)labels->length);
    if(current - word >= 1024) die("overflowed token length in %s, line %d", input, line_num);
    fclose(fp);
    int i;
    for(i = 0; i < features->length; i++) {
        feature_t* feature = (feature_t*) vector_get(features, i);
        vector_sort(feature->examples, comparator);
        vector_remove_duplicates(feature->examples);
        vector_optimize(feature->examples);
    }
    for(i = 0; i < examples->length; i++) {
        example_t* example = (example_t*) vector_get(examples, i);
        example->label_score = malloc(sizeof(double) * labels->length);
        int j;
        for(j = 0; j < labels->length; j++) example->label_score[j] = 0.0;
    }
    hashtable_free(text_to_label);
    hashtable_free(text_to_feature);
    return 0;
}

int normalize(vector_t* examples) {
    int i;
    double sum = 0.0;
    for(i = 0; i < examples->length; i++) {
        example_t* example = (example_t*) vector_get(examples, i);
        sum += example->weight;
    }
    if(sum < 1e-5 && sum > -1e-5) die("sum of weights is zero");
    for(i = 0; i < examples->length; i++) {
        example_t* example = (example_t*) vector_get(examples, i);
        example->weight /= sum;
    }
    return 0;
}

classifier_t* find_best_classifier(vector_t* examples, vector_t* features, vector_t* labels) {
    int i, j, k;
    classifier_t* output = malloc(sizeof(classifier_t));
    output->feature = NULL;
    output->label = -1;
    output->error = 1.0;
    double have_label[labels->length];
    for(j = 0; j < labels->length; j++) have_label[j] = 0.0;
    for(i = 0; i < examples->length; i++) {
        example_t* example = (example_t*) vector_get(examples, i);
        for(j = 0; j < labels->length; j++) {
            if(example->label == j) have_label[j] += example->weight;
        }
    }
    // error(feature) = examples(feature,!label) + examples(!feature, label)
    // examples(!feature, label) = examples(label) - examples(feature, label)
    for(i = 0; i < features->length; i++) {
        feature_t* feature = (feature_t*) vector_get(features, i);
        if(feature->examples->length < 4) continue;
        double error[labels->length];
        double feature_not_label[labels->length];
        double feature_label[labels->length];
        double not_feature_label[labels->length];
        for(j = 0; j < labels->length; j++) {
            feature_label[j] = 0.0;
            feature_not_label[j] = 0.0;
            not_feature_label[j] = 0.0;
        }
        for(j = 0; j < feature->examples->length; j++) {
            example_t* example = (example_t*) vector_get(examples, vector_get_int32_t(feature->examples, j));
            for(k = 0; k < labels->length; k++) {
                if(example->label == k) feature_label[k] += example->weight;
                else feature_not_label[k] += example->weight;
            }
        }
        for(j = 0; j < labels->length; j++) {
            not_feature_label[j] = have_label[j] - feature_label[j];
            error[j] = feature_not_label[j] + have_label[j] - feature_label[j];
            if(output->feature == NULL || error[j] < output->error) {
                output->feature = feature;
                output->label = j;
                output->error = error[j];
            }
        }
    }
    output->alpha = log((1.0 - output->error) / (output->error)) + log(labels->length - 1.0);
    return output;
}

int update_weights(vector_t* examples, classifier_t* classifier) {
    int i;
    int* flags = malloc(sizeof(int) * examples->length);
    for(i = 0; i < examples->length; i++) flags[i] = 0;
    for(i = 0; i < classifier->feature->examples->length; i++) {
        example_t* example = (example_t*) vector_get(examples, vector_get_int32_t(classifier->feature->examples, i));
        if(example->label == classifier->label) flags[example->id] = 1;
    }
    for(i = 0; i < examples->length; i++) {
        if(flags[i] == 0) {
            example_t* example = (example_t*) vector_get(examples, i);
            example->weight *= exp(classifier->alpha);
        }
    }
    return 0;
}

double compute_error(vector_t* examples, classifier_t* classifier, vector_t* labels) {
    int i;
    for(i = 0; i < classifier->feature->examples->length; i++) {
        example_t* example = (example_t*) vector_get(examples, vector_get_int32_t(classifier->feature->examples, i));
        example->label_score[classifier->label] += classifier->alpha;
    }
    int errors = 0;
    for(i = 0; i < examples->length; i++) {
        example_t* example = vector_get(examples, i);
        int j;
        int argmax = 0;
        double max = example->label_score[0];
        for(j = 1; j < labels->length; j++) {
            if(example->label_score[j] > max) {
                max = example->label_score[j];
                argmax = j;
            }
        }
        if(argmax != example->label) errors += 1;
    }
    return (double) errors / examples->length;
}

int main(int argc, char** argv) {
    if(argc != 4 && argc != 3) {
        fprintf(stderr, "USAGE: training: %s <iterations> <model> <trainning_data>\n", argv[0]);
        //fprintf(stderr, "       prediction: %s <model> <test_data>\n", argv[0]);
        exit(1);
    }
    vector_t* features = vector_new(1024);
    vector_t* examples = vector_new(1024);
    vector_t* labels = vector_new(1024);
    read_examples(argv[3], features, examples, labels);
    int iterations = strtol(argv[1], NULL, 10);
    int i;
    for(i = 0; i < iterations; i++) {
        normalize(examples);
        classifier_t* classifier = find_best_classifier(examples, features, labels);
        update_weights(examples, classifier);
        double error = compute_error(examples, classifier, labels);
        label_t* label = (label_t*) vector_get(labels, classifier->label);
        fprintf(stdout, "%d err=%f werr=%f %s:%s alpha=%g\n", i, error, classifier->error, classifier->feature->text, label->text, classifier->alpha);
    }
    vector_free(labels);
    vector_free(examples);
    vector_free(features);
    return 0;
}
