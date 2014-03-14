ICSIBoost: Open-source implementation of Boostexter (Adaboost based classifier)
===============================================================================

Boosting is a meta-learning approach that aims at combining an ensemble of weak classifiers to form a strong classifier. Adaptive Boosting (Adaboost) is a greedy search for a linear combination of classifiers by overweighting the examples that are misclassified by each classifier. icsiboost implements Adaboost over stumps (one-level decision trees) on discrete and continuous attributes (words and real values). See http://en.wikipedia.org/wiki/AdaBoost and the papers by Y. Freund and R. Schapire for more details. This approach is one of the most efficient and simple to combine continuous and nominal values. Our implementation is aimed at allowing training from millions of examples by hundreds of features (or millions of sparse features) in a reasonable time/memory. It includes classification time code for c, python and java.

Here is an excellent tutorial on Boosting: http://nips.cc/Conferences/2007/Program/event.php?ID=575

News
----

* 2014-03-14: Migrated to github from googlecode
* 2012-05-28: Added C++ API for prediction. See test_api.cc and IcsiBoost.h
* 2012-05-21: Switched to dual license: GPL and BSD. Choose the license that best fits your project. Old revisions are GPL only.
* 2011-02-15: icsiboost is now in archlinux (aur package icsiboost-svn)
* 2010-12-23: QualityTesting tracks svn releases for time, memory and error rate on a sample dataset.
* 2010-12-10: Added script to convert example files from icsiboost to svm_light/mlcomp format including ngram/cutoff management ([http://icsiboost.googlecode.com/svn-history/trunk/icsiboost/src/icsiboost_to_svm.py icsiboost_to_svm.py])
* 2010-10-20: Fixed a bug with continuous features (r159) and removed the need for the --display-maxclass option: it is now the default when examples have a single label (r160). The old way of computing the error rate is still used in the multilabel scenario.
* 2010-10-03: Added support for Solaris (r154), also added win32 downloads (requires cygwin1.dll)
* 2010-05-05: Maintenance release: better error handling of discrete features declared in names file (r130).
* 2010-01-24: Added Stanislas' patch to display error rates based on argmax instead of sign decisions.
* 2009-10-19: Added a rudimentary java implementation of the classifier.
* 2009-10-10: Released the optimal_threshold.pl script to get a better decision threshold on unbalanced data (for binary problems only).
* 2009-07-29: There is now a decoder in pure python. It's quite slow (and could be optimized), but is useful for small projects in python and educational purposes.
* 2009-04-08 WARNING: On multiclass problems, icsiboost does not compute the error rates the same way boostexter does. This does not result in lower performing models, and an option for getting compatible values will be implemented in the future.
* 2009-03-30 You can now specify the type of text expert and its length on a per-column basis in the names file (previously set globally with -N ngram -W 3...). Example: "words:text." becomes "words:text:expert_type=ngram expert_length=5 cutoff=3." which is equivalent to -N ngram -W 5 --cutoff 3, only for that the words column.
* You should use the svn version to get the latest fixes ([http://code.google.com/p/icsiboost/source/list change log]).
* do not use r96: a bug made training fail (all users should upgrade to r102 which fixes major bugs)
* **WARNING: if you trained a model with -N ngram -W length, you must pass the same options at test time, otherwise the related weak classifiers will be ignored (unless you specify it in the names file).**

Get and Compile (you need PCRE >= 01-December-2003):

```
    svn checkout http://icsiboost.googlecode.com/svn/trunk/ .
    cd icsiboost
    autoreconf
    automake -a
    ./configure CFLAGS=-O3
    make
```

Program usage (revision r124):
------------------------------

```
    USAGE: ./icsiboost [options] -S <stem>
      --version               print version info
      -S <stem>               defines model/data/names stem
      -n <iterations>         number of boosting iterations (also limits test time classifiers, if model is not packed)
      -E <smoothing>          set smoothing value (default=0.5)
      -V                      verbose mode
      -C                      classification mode -- reads examples from <stdin>
      -o                      long output in classification mode
      -N <text_expert>        choose a text expert between fgram, ngram and sgram (also "<name>:text:expert_type=<x>" in the .names)
      -W <ngram_length>       specify window length of text expert (also "<name>:text:expert_length=<n>" in .names)
      --dryrun                only parse the names file and the data file to check for errors
      --cutoff <freq>         ignore nominal features occuring unfrequently (also "<name>:text:cutoff=<freq>" in .names)
      --drop <regex>          drop text features that match a regular expression (also "<name>:text:drop=<regex>" in .names)
      --no-unk-ngrams         ignore ngrams that contain the "unk" token
      --jobs <threads>        number of threaded weak learners
      --do-not-pack-model     do not pack model (this is the default behavior)
      --pack-model            pack model (for boostexter compatibility)
      --output-weights        output training examples weights at each iteration
      --posteriors            output posterior probabilities instead of boosting scores
      --model <model>         save/load the model to/from this file instead of <stem>.shyp
      --resume                resume training from a previous model (can use another dataset for adaptation)
      --train <file>          bypass the <stem>.data filename to specify training examples
      --dev <file>            bypass the <stem>.dev filename to specify development examples
      --test <file>           bypass the <stem>.test filename to specify test examples
      --names <file>          use this column description file instead of <stem>.names
      --ignore <columns>      ignore a comma separated list of columns (synonym with "ignore" in names file)
      --ignore-regex <regex>  ignore columns that match a given regex
      --only <columns>        use only a comma separated list of columns (synonym with "ignore" in names file)
      --only-regex <regex>    use only columns that match a given regex
      --interruptible         save model after each iteration in case of failure/interruption
      --optimal-iterations    output the model at the iteration that minimizes dev error (or max fmeasure if specified)
      --max-fmeasure <class>  display maximum f-measure of specified class instead of error rate
      --fmeasure-beta <float> specify weight of recall compared to precision in f-measure
      --abstaining-stump      use abstain-on-absence text stump (experimental)
      --no-unknown-stump      use abstain-on-unknown continuous stump (experimental)
      --sequence              generate column __SEQUENCE_PREVIOUS from previous prediction at test time (experimental)
      --anti-prior            set initial weights to focus on classes with a lower prior (experimental)
      --display-maxclass      display the classification rate obtained by selecting only one class by exemple, the one that obtains the maximum score (boostexter-compatible)
```

The input data is defined in a format similar to the UCI repository (http://www.ics.uci.edu/~mlearn/MLRepository.html). You will have to remove blank lines, comments and add a period at the end of each lines. The data must contain a stem.data file with training examples, a stem.names file describing the classes/features and may contain a .dev and .test file that will be used for error rate computation.

Currently, icsiboost is used at ICSI (http://www.icsi.berkeley.edu/) for sentence boundary detection and other speech understanding related classification tasks. If you find icsiboost useful, please send us an email describing your work.

A few papers referring to icsiboost
-----------------------------------

  * Eric Charton, Nathalie Camelin, Rodrigo Acuna-Agost, Pierre Gotab, Remy Lavalley, Remy Kessler and Silvia Fernandez, "[http://deft08.limsi.fr/actes/deft-05_charton-et-al08_v5.pdf Pretraitements classiques ou par analyse distributionnelle: application aux methodes de classification automatique deployees pour DEFT08]", TALN 2008
  * Benoit Favre, Dilek Hakkani-Tur, Slav Petrov and Dan Klein, "[http://www.eecs.berkeley.edu/~petrov/data/slt08.pdf Efficient Sentence Segmentation Using Syntactic Features]", SLT 2008
  * Jachym Koliar, "[http://ar.kky.zcu.cz/en/publications/1/JachymKolar_2008_AComparisonof.pdf A Comparison of Language Models for Dialog Act Segmentation of Meeting Transcripts]", TSD 2008
  * Susmit Biswas, Diana Franklin, Alan Savage, Ryan Dixon, Timothy Sherwood, Frederic T. Chong, "[http://www.cs.ucsb.edu/~susmit/papers/isca142-biswas.pdf Multi-Execution: Multicore Caching for Data-Similar Executions]", ISCA'09
  * Doetsch, P. and Buck, C. and Golik, P. and Hoppe, N. and Kramp, M. and Laudenberg, J. and Oberdorfer, C. and Steingrube, P. and Forster, J. and Mauser, A., "[http://clopinet.com/isabelle/Projects/KDDcup09/Papers/Aachen-paper6.pdf Logistic Model Trees with AUCsplit Criterion for the KDD Cup 2009 Small Challenge]"
  * Dufour R., Estève Y., Deléglise P., Béchet F. (2009), "Local and global models for spontaneous speech segment detection and characterization", IEEE ASRU 2009, December 13-17, 2009, Merano (Italy).
  * For more papers, see http://scholar.google.com/scholar?q=icsiboost

To cite icsiboost, please use:
```
    @misc{icsiboost,
    title={{Icsiboost}},
    author={Benoit Favre and Dilek Hakkani-T{\"u}r and Sebastien Cuendet},
    howpublished={\url{http://code.google.come/p/icsiboost}},
    year={2007}
    }
```

Notes
-----

  * 64-bit compatible
  * multi-threaded training
  * works on Linux, MacOS, Windows and Solaris

Missing
-------

  * more performance measures (average F-measure, one-class error rate, AUC)
  * scored text (see http://github.com/benob/tbb-boost for equivalent)
  * discrete !AdaBoost.MR/MH (unlikely to be implemented)
  * unlearning (for online learning)
  * n-fold

Here is a picture from google analytics of the distribution of traffic sources that come to this website (as of Feb 2010):
!(http://icsiboost.googlecode.com/files/icsiboost_traffic_sources.png)
