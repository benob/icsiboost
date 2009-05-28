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
 * .names), only supports discrete features, does not generate do any
 * preprocessing (ngrams...)
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "utils/hashtable.h"
#include "utils/vector.h"
#include "utils/threads.h"

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

int pointer_comparator(const void* a, const void* b) {
    void* aa = *((void**)a);
    void* bb = *((void**)b);
    if(aa < bb) return -1;
    if(aa > bb) return +1;
    return 0;
}

int num_examples_comparator(const void* a, const void* b) {
    feature_t* aa = *((feature_t**) a);
    feature_t* bb = *((feature_t**) b);
    if(aa->examples->length > bb->examples->length) return +1;
    if(aa->examples->length < bb->examples->length) return -1;
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
            if(examples->length % 1000 == 0) {
                fprintf(stdout, "\rexamples: %d, features: %d, labels: %d", 
                        (int)examples->length, (int)features->length, (int)labels->length);
                fflush(stdout);
            }
        }
        line_num += 1;
    }
    if(current - word >= 1024) die("overflowed token length in %s, line %d", input, line_num);
    fclose(fp);
    free(example);
    fprintf(stdout, "\rexamples: %d, features: %d, labels: %d\n", 
            (int)examples->length, (int)features->length, (int)labels->length);
    int i;
    for(i = 0; i < features->length; i++) {
        feature_t* feature = (feature_t*) vector_get(features, i);
        vector_sort(feature->examples, pointer_comparator);
        vector_remove_duplicates(feature->examples);
        vector_optimize(feature->examples);
    }
    for(i = 0; i < examples->length; i++) {
        example_t* example = (example_t*) vector_get(examples, i);
        example->label_score = malloc(sizeof(double) * labels->length);
        int j;
        for(j = 0; j < labels->length; j++) example->label_score[j] = 0.0;
    }
    vector_sort(features, num_examples_comparator); // always start with larger jobs
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
    if(sum < 1e-11 && sum > -1e-11) die("sum of weights is zero");
    for(i = 0; i < examples->length; i++) {
        example_t* example = (example_t*) vector_get(examples, i);
        example->weight /= sum;
    }
    return 0;
}

typedef struct best_classifier_workspace {
    semaphore_t* feature_semaphore;
    vector_t* features;
    vector_t* examples;
    double* have_label;
    feature_t* argmin;
    int argmin_label;
    double min;
    int num_labels;
} best_classifier_workspace_t;

void* best_classifier_thread(void* input) {
    best_classifier_workspace_t* workspace = (best_classifier_workspace_t*) input;
    double error[workspace->num_labels];
    double feature_not_label[workspace->num_labels];
    double feature_label[workspace->num_labels];
    int j, k;
    while(1) { // features available
        int current = semaphore_eat(workspace->feature_semaphore);
        if(current < 0) break;
        feature_t* feature = (feature_t*) vector_get(workspace->features, current);
        for(j = 0; j < workspace->num_labels; j++) {
            feature_label[j] = 0.0;
            feature_not_label[j] = 0.0;
        }
        for(j = 0; j < feature->examples->length; j++) {
            example_t* example = (example_t*) vector_get(workspace->examples, 
                    vector_get_int32_t(feature->examples, j));
            for(k = 0; k < workspace->num_labels; k++) {
                if(example->label == k) feature_label[k] += example->weight;
                else feature_not_label[k] += example->weight;
            }
        }
        for(j = 0; j < workspace->num_labels; j++) {
            error[j] = feature_not_label[j] + workspace->have_label[j] - feature_label[j];
            if(workspace->argmin == NULL || error[j] < workspace->min
                    || (error[j] == workspace->min && feature->id < workspace->argmin->id)) {
                workspace->argmin = feature;
                workspace->argmin_label = j;
                workspace->min = error[j];
            }
        }
    }
	pthread_exit(NULL);
}

classifier_t* find_best_classifier_threaded(vector_t* examples, vector_t* features, vector_t* labels, int num_threads) {
    int i, j;
    classifier_t* output = malloc(sizeof(classifier_t));
    output->feature = NULL;
    output->label = 0;
    output->error = 1.0;
    double have_label[labels->length];
    for(j = 0; j < labels->length; j++) have_label[j] = 0.0;
    for(i = 0; i < examples->length; i++) {
        example_t* example = (example_t*) vector_get(examples, i);
        for(j = 0; j < labels->length; j++) {
            if(example->label == j) have_label[j] += example->weight;
        }
    }
    semaphore_t* semaphore = semaphore_new(features->length - 1);
    semaphore->block_on_zero = 0;
    best_classifier_workspace_t workspace[num_threads];
    for(i = 0; i < num_threads; i++) {
        workspace[i].feature_semaphore = semaphore;
        workspace[i].features = features;
        workspace[i].examples = examples;
        workspace[i].have_label = have_label;
        workspace[i].argmin = NULL;
        workspace[i].min = 1.0;
        workspace[i].argmin_label = 0;
        workspace[i].num_labels = labels->length;
    }
    pthread_t threads[num_threads];
    for(i = 0; i < num_threads; i++) {
		pthread_create(&threads[i], NULL, best_classifier_thread, &workspace[i]);
    }
    for(i = 0; i < num_threads; i++) {
        void* tmp;
        pthread_join(threads[i], &tmp);
        if(output->feature == NULL || workspace[i].min < output->error 
                || (workspace[i].min == output->error && workspace[i].argmin->id < output->feature->id)) {
            output->error = workspace[i].min;
            output->label = workspace[i].argmin_label;
            output->feature = workspace[i].argmin;
        }
    }
    semaphore_free(semaphore);
    output->alpha = log((1.0 - output->error) / (output->error)) + log(labels->length - 1.0);
    return output;
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
    for(i = 0; i < features->length; i++) {
        feature_t* feature = (feature_t*) vector_get(features, i);
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
            if(output->feature == NULL || error[j] < output->error
                    || (error[j] == output->error && feature->id < output->feature->id)) {
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

/*int perform_predictions(const char* input, const char* model_name) {
    vector_t* classifiers = vector_new(16);
    FILE* model = fopen(model_name, "r");
    if(model == NULL) die("reading model '%s'", model_name);
    string_t* label_list = string_readline(model);
    array_t* labels = string_split(label_list, " ", 0);
    string_t* line = NULL;
    while(NULL != (line = string_readline(model))) {
        classifier_t* classifier = malloc(sizeof(classifier_t));
        classifier->
    }
    fclose(model);
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
            if(examples->length % 1000 == 0) {
                fprintf(stdout, "\rexamples: %d, features: %d, labels: %d", 
                        (int)examples->length, (int)features->length, (int)labels->length);
                fflush(stdout);
            }
        }
        line_num += 1;
    }
    if(current - word >= 1024) die("overflowed token length in %s, line %d", input, line_num);
    fclose(fp);
    free(example);
    fprintf(stdout, "\rexamples: %d, features: %d, labels: %d\n", 
            (int)examples->length, (int)features->length, (int)labels->length);
    int i;
    for(i = 0; i < features->length; i++) {
        feature_t* feature = (feature_t*) vector_get(features, i);
        vector_sort(feature->examples, pointer_comparator);
        vector_remove_duplicates(feature->examples);
        vector_optimize(feature->examples);
    }
    for(i = 0; i < examples->length; i++) {
        example_t* example = (example_t*) vector_get(examples, i);
        example->label_score = malloc(sizeof(double) * labels->length);
        int j;
        for(j = 0; j < labels->length; j++) example->label_score[j] = 0.0;
    }
    vector_sort(features, num_examples_comparator); // always start with larger jobs
    hashtable_free(text_to_label);
    hashtable_free(text_to_feature);
    return 0;
}*/

int main(int argc, char** argv) {
    if(argc != 5 && argc != 3) {
        fprintf(stderr, "USAGE: training: %s <threads> <iterations> <model> <trainning_data>\n", argv[0]);
        //fprintf(stderr, "       prediction: %s <model> <test_data>\n", argv[0]);
        exit(1);
    }
    vector_t* features = vector_new(1024);
    vector_t* examples = vector_new(1024);
    vector_t* labels = vector_new(1024);
    read_examples(argv[4], features, examples, labels);
    int num_threads = strtol(argv[1], NULL, 10);
    int iterations = strtol(argv[2], NULL, 10);
    int i;
    FILE* model = fopen(argv[3], "w");
    if(model == NULL) die("writing model file '%s'", argv[3]);
    for(i = 0; i < labels->length; i++) {
        label_t* label = (label_t*) vector_get(labels, i);
        fprintf(model, "%s ", label->text);
    }
    fprintf(model, "\n");
    for(i = 0; i < iterations; i++) {
        normalize(examples);
        classifier_t* classifier = NULL;
        if(num_threads > 1) classifier = find_best_classifier_threaded(examples, features, labels, num_threads);
        else classifier = find_best_classifier(examples, features, labels);
        update_weights(examples, classifier);
        double error = compute_error(examples, classifier, labels);
        label_t* label = (label_t*) vector_get(labels, classifier->label);
        fprintf(stdout, "%d err=%f %s:%s\n", i + 1, error, classifier->feature->text, label->text);
        fprintf(model, "%f %s %s\n", classifier->alpha, label->text, classifier->feature->text);
        fflush(model);
    }
    fclose(model);
    vector_free(labels);
    vector_free(examples);
    vector_free(features);
    return 0;
}
