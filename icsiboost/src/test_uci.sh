test -f adult.data || wget ftp://ftp.ics.uci.edu/pub/machine-learning-databases/adult/adult.*
./icsiboost -j 2 -n 100 -S ./adult
