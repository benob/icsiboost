#!/bin/bash

cat test_log/*.test | grep ^r | LANG=C sort > all.test
cat test_log/*.train | grep ^r | LANG=C sort > all.train
cat test_log/*.perf | LANG=C sort > all.perf
join all.perf all.train | join - all.test | sort -k2 -tr -n > all.analysis
cut -d " " -f 1,2,4,8,27,31 all.analysis | tr " " ";" > all.csv

