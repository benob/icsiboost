test -f adult.data || wget ftp://ftp.ics.uci.edu/pub/machine-learning-databases/adult/adult.*
`dirname $0`/icsiboost -n 100 -S ./adult
#wget http://www.csie.ntu.edu.tw/~cjlin/libsvmtools/datasets/binary/a1a
#wget http://www.csie.ntu.edu.tw/~cjlin/libsvmtools/datasets/binary/a1a.t
