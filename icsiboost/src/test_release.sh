#!/bin/bash

if [ $# != 1 ]
then
    echo "USAGE: $0 release" >&2
    exit 1
fi

export release=$1
rm -rf adult.shyp adult.test.out test_source
mkdir -p test_log

echo "==== checkout ===="
svn checkout -r $release http://icsiboost.googlecode.com/svn/trunk/ test_source || exit 1
pushd test_source/icsiboost/
echo "==== configure ===="
./configure || exit 2
echo "==== compile ===="
make || exit 3
popd
echo "==== training ===="
format="$release %E %e %S %U %P %M %t %K %D %p %X %Z %F %R %W %c %w %I %O %r %s %k %x"
/usr/bin/time -f "$format" -o test_log/$release.train test_source/icsiboost/src/icsiboost -S adult -n 100 || exit 4
echo "==== testing ===="
/usr/bin/time -f "$format" -o test_log/$release.test test_source/icsiboost/src/icsiboost -S adult -C < adult.test > adult.test.out || exit 5
echo "==== test error rate ===="
cat adult.test.out | awk -vr=$release '{if($1 == 1 && $3 < $4 || $2 == 1 && $3 > $4){e++}}END{print r, e/NR}' | tee test_log/$release.perf || exit 6

rm -rf adult.shyp adult.test.out test_source

