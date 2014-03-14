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

# decoder for CRF++, Benoit Favre <benoit.favre@gmail.com>, 2009-12-07
# train with the -t option and use the text model
# warning: the decoder is slow and memory inefficient

import sys, re

class Classifier:
    def __init__(self, model_name):
        self.load_model(model_name)

    def load_model(self, name):
        state = "header"
        num = 0
        self.labels = []
        self.templates = []
        self.features = {}
        self.weights = []
        for line in open(name):
            num += 1
            line = line.strip()
            if line == "":
                if state == "header": state = "labels"
                elif state == "labels": state = "templates"
                elif state == "templates": state = "features"
                elif state =="features": state = "weights"
                else:
                    sys.stderr.write('ERROR: reading model "%s", line %d\n'% (name, num))
                    return
            else:
                if state == "labels":
                    self.labels.append(line)
                elif state == "templates":
                    self.templates.append(line)
                elif state == "features":
                    tokens = line.split()
                    self.features[tokens[1]] = int(tokens[0])
                elif state == "weights":
                    self.weights.append(float(line))

    def next_example(self, input):
        example = []
        for line in input:
            line = line.strip()
            if line == "":
                return example
            else:
                example.append(line.split())
        return None
    
    def read_examples(self, input):
        example = self.next_example(input)
        while example != None:
            yield example
            example = self.next_example(input)

    def _build_feature(self, example, line, column):
        if line >= 0 and line < len(example):
            if column > 0 and column < len(example[line]):
                return example[line][column]
        return "_B%d" % line
        
    def classify(self, example, beam_size=None):
        num_labels = len(self.labels)
        unigram = []
        bigram = []
        for i in xrange(len(example)):
            unigram.append([0.0 for x in xrange(num_labels)])
            bigram.append([0.0 for x in xrange(num_labels * num_labels)])
            for template in self.templates:
                feature = re.sub(r'%x\[(-?\d+),(\d+)\]', lambda x: self._build_feature(example, i + int(x.group(1)), int(x.group(2))), template)
                if feature not in self.features or (i == 0 and template.startswith("B")):
                    continue
                if feature.startswith("U"):
                    for j in xrange(num_labels):
                        unigram[i][j] += self.weights[self.features[feature] + j]
                elif feature.startswith("B"):
                    for j in xrange(num_labels):
                        for k in xrange(num_labels):
                            bigram[i][j * num_labels + k] += self.weights[self.features[feature] + j * num_labels + k]
        previous = []
        for i in xrange(len(example)):
            current = []
            for j in xrange(num_labels):
                if len(previous) == 0:
                    current.append([None, j, unigram[i][j], None])
                else:
                    for path in previous:
                        current.append([path[1], j, path[2] + unigram[i][j] + bigram[i][path[1] * num_labels + j], path])
            if beam_size and len(current) > beam_size:
                current = sorted(current, lambda x, y: cmp(y[2], x[2]))[:beam_size]
            previous = current
        max = 0
        argmax = None
        for path in previous:
            if argmax == None or path[2] > max:
                max = path[2]
                argmax = path
        
        output = []
        while argmax:
            output.insert(0, argmax[1])
            argmax = argmax[3]
        return [self.labels[x] for x in output], max

if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.stderr.write('USAGE: %s <model_name>\n' % sys.argv[0])
        sys.exit(1)
    classifier = Classifier(sys.argv[1])
    for example in classifier.read_examples(sys.stdin):
        labels, score = classifier.classify(example)
        for line,label in zip(example, labels):
            print "\t".join(line) + "\t" + label
        print
