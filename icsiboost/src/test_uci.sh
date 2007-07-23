test -f adult.data || wget ftp://ftp.ics.uci.edu/pub/machine-learning-databases/adult/adult.*
`dirname $0`/icsiboost -n 100 -S ./adult
