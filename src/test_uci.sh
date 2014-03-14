#!/bin/sh
[ -f adult.names ] || wget http://archive.ics.uci.edu/ml/machine-learning-databases/adult/adult.names -O adult.names
[ -f adult.data ] || wget http://archive.ics.uci.edu/ml/machine-learning-databases/adult/adult.data -O adult.data
[ -f adult.test ] || wget http://archive.ics.uci.edu/ml/machine-learning-databases/adult/adult.test -O adult.test
./icsiboost -n 100 -S adult
