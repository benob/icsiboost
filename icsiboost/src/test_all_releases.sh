#!/bin/bash

# don't forget to put the right release number
# you should run this script in a directory of its own
mkdir -p test_log
for i in `seq 172`
do
    ./test_release.sh r$i 2>&1 | tee test_log/r$i.log
    echo r$i $? > test_log/r$i.exit
done
