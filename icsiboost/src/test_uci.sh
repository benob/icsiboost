test -f adult.data || wget ftp://ftp.ics.uci.edu/pub/machine-learning-databases/adult/adult.*
`dirname $0`/icsiboost -j 2 -n 100 -S ./adult
