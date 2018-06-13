#!/usr/bin/env python
# -*- coding: utf-8 -*-

# Copyright (C) (2009) (Benoit Favre) <favre@icsi.berkeley.edu>
#
# This program is free software; you can redistribute it and/or 
# modify it under the terms of the GNU Lesser General Public License 
# as published by the Free Software Foundation; either 
# version 2 of the License, or (at your option) any later 
# version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU Lesser General Public License for more details.
#
# You should have received a copy of the GNU Lesser General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA. */

import sys, re, math

# this script implements the prediction side of icsiboost in python
# supports continuous features and ngrams (see the main)
# sgrams and fgrams are not supported yet, but easy to implement (see generate_ngram())

class Feature:
    def __init__(self, alpha, column, value=None, threshold=None):
        self.alpha = alpha
        self.column = column
        self.value = value
        self.threshold = threshold
        self.c0 = []
        self.c1 = []
        self.c2 = []

class Classifier:
    def __init__(self, stem=None):
        self.features = []
        self.names = {}
        self.classes = []
        if stem:
            self.load_model(stem)

    def load_model(self, stem):
        self.read_names(stem + ".names")
        self.read_shyp(stem + ".shyp")

    def read_names(self, names_file):
        classes = None
        columns = []
        for line in open(names_file).xreadlines():
            line = line.strip()
            if classes == None:
                classes = [x.strip() for x in re.sub('\.$', '', line).split(',')]
                self.classes = classes
            else:
                name = re.split(r':', line)[0]
                self.names[name] = len(self.names)
        #print "NAMES", self.names
    def read_shyp(self, shyp_file):
        num_classifiers = None
        feature = None
        line_num = 0
        for line in open(shyp_file).xreadlines():
            line_num += 1
            line = line.strip()
            if line == "":
                continue
            tokens = line.split()
            found_sgram = re.search(r'^\s*(\S+)\s+Text:SGRAM:([^:]+):(.*?) *$', line)
            found_threshold = re.search(r'^\s*(\S+)\s+Text:THRESHOLD:([^:]+):', line)
            if num_classifiers == None:
                num_classifiers = int(line)
            elif found_sgram:
                alpha = float(found_sgram.group(1))
                column = found_sgram.group(2)
                value = found_sgram.group(3)
                feature = Feature(alpha, column, value)
                self.features.append(feature)
            elif found_threshold:
                alpha = float(found_threshold.group(1))
                column = found_threshold.group(2)
                feature = Feature(alpha, column)
                self.features.append(feature)
            elif feature != None and len(tokens) == len(self.classes):
                if feature.c0 == []: feature.c0 = [float(x) * feature.alpha for x in tokens]
                elif feature.c1 == []: feature.c1 = [float(x) * feature.alpha for x in tokens]
                elif feature.c2 == []: feature.c2 = [float(x) * feature.alpha for x in tokens]
                else:
                    sys.stderr.write('ERROR: too many weights, in %s, line %d\n' % (shyp_file, line_num))
                    return None
            elif feature != None and feature.value == None and feature.threshold == None and len(tokens) == 1:
                feature.threshold = float(tokens[0])
            else:
                sys.stderr.write('ERROR: unsupported classifier, in %s, line %d\n' % (shyp_file, line_num))
                return None

    # example should be [set(), set(), ...] with a set for each column (or anything which has the "in" operator)
    def compute_scores(self, example):
        #print "rrrr", example
        scores = [0.0 for x in self.classes]
        for feature in self.features:
            if feature.value != None:
                #print feature.value, example[self.names[feature.column]]
                if feature.value in example[self.names[feature.column]]:
                    for i in range(len(feature.c1)):
                        scores[i] += feature.c1[i]
                else:
                    for i in range(len(feature.c0)):
                        scores[i] += feature.c0[i]
            elif feature.threshold != None:
                for value in example[self.names[feature.column]]:
                    if value == '?':
                        #print feature.threshold, value, feature.c0
                        for i in range(len(feature.c0)):
                            scores[i] += feature.c0[i]
                    elif feature.threshold >= float(value):
                        #print feature.threshold, value, feature.c1
                        for i in range(len(feature.c1)):
                            scores[i] += feature.c1[i]
                    else:
                        #print feature.threshold, value, feature.c2
                        for i in range(len(feature.c0)):
                            scores[i] += feature.c2[i]
        return [x / len(self.features) for x in scores]

    def compute_posteriors(self, example):
        scores = self.compute_scores(example)
        #print "eeeee", len(self.features), scores
        return [1.0 / (1.0 + math.exp(-2.0 * x * len(self.features))) for x in scores]

    def classify(self, example):
        scores = self.compute_scores(example)
        max = 0
        argmax = None
        for i in range(len(self.classes)):
            if argmax == None or max < scores[i]:
                max = scores[i]
                argmax = i
        return self.classes[argmax]

def generate_ngram(words, n):
    output = []
    for i in xrange(len(words)):
        current = []
        for j in xrange(n):
            if i + j >= len(words): continue
            current.append(words[i + j])
            output.append('#'.join(current))
    return output

if __name__ == '__main__':
    if len(sys.argv) < 2:
        sys.stderr.write('USAGE: %s <stem>\n' % sys.argv[0])
        sys.exit(1)
    classifier = Classifier(sys.argv[1])
    for line in sys.stdin.xreadlines():
        #print "zzz", line
        columns = line.strip().split(",")
        columns[-1] = re.sub(r'\.$', '', columns[-1].strip())
        scores = classifier.compute_posteriors([set(generate_ngram(x.split(), 3)) for x in columns])
        for i in range(len(classifier.classes)):
            if i != 0:  sys.stdout.write(' ')
            if columns[-1] == classifier.classes[i]:
                sys.stdout.write('1')
            else:
                sys.stdout.write('0')

        #print " ".join([str(x) for x in scores])
        for x in scores:
            sys.stdout.write(" %.12f" % x)
        print ""

