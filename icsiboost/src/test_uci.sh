#!/bin/sh
wget http://archive.ics.uci.edu/ml/machine-learning-databases/adult/adult.names
wget http://archive.ics.uci.edu/ml/machine-learning-databases/adult/adult.data
wget http://archive.ics.uci.edu/ml/machine-learning-databases/adult/adult.test
./icsiboost -n 100 -S adult
