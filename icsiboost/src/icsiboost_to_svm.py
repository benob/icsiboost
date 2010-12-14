#!/usr/bin/env python2
import sys, re

def get_ngrams(words, n):
    output = []
    for i in xrange(len(words)):
        ngram = []
        for j in xrange(n):
            if i + j < len(words):
                ngram.append(words[i + j])
                output.append("_".join(ngram))
    return output

def get_fgrams(words, n):
    output = []
    for i in xrange(len(words) - n + 1):
        output.append("_".join(words[i:i+n]))
    return output

def get_sgrams(words, n):
    output = []
    for i in xrange(len(words)):
        for j in xrange(1, n + 2):
            if i + j < len(words):
                output.append(words[i] + "_" + words[i + j])
    return output

def load_names(filename, ignore, ignore_regex, only, only_regex):
    fp = open(filename)
    output = []
    labels = {}
    seen_labels = False
    for line in fp:
        if seen_labels:
            tokens = [x.strip() for x in line.strip().split(":")]
            tokens[1] = re.sub(r'\.$', '', tokens[1]).strip()
            if (tokens[0] in ignore) \
                    or (ignore_regex and re.search(ignore_regex, tokens[0])) \
                    or (only and tokens[0] not in only) \
                    or (only_regex and not re.search(only_regex, tokens[0])) \
                    or tokens[1] == "ignore":
                output.append(None)
            elif tokens[1] in ("continuous", "text"):
                output.append([tokens[0], tokens[1]])
            else:
                output.append([tokens[0], "set"])
        else:
            tokens = [x.strip() for x in line.strip().split(",")]
            tokens[-1] = re.sub(r'\.$', '', tokens[-1]).strip()
            for i in xrange(len(tokens)):
                labels[tokens[i]] = i + 1
            seen_labels = True
    fp.close()
    return output, labels

def usage():
    print "USAGE: %s --names <names-file> [options] <input_files>" % sys.argv[0]
    print "OPTIONS:"
    print "  --names <names-file>    feature type specification file"
    print "  -N <text_expert>        choose a text expert between fgram, ngram and sgram"
    print "  -W <ngram_length>       specify window length of text expert"
    print "  --keep-empty-examples   empty examples are removed by default"
    print "  -o <feature-dict>       output feature dictionary"
    print "  -i <feature-dict>       input feature dictionary"
    print "  -e <output-extension>   extension of output files (by concatenation, defaults to .svm)"
    print "  --cutoff <freq>         ignore nominal features occuring unfrequently"
    print "  --ignore <columns>      ignore a comma separated list of columns"
    print "  --ignore-regex <regex>  ignore columns that match a given regex"
    print "  --only <columns>        use only a comma separated list of columns"
    print "  --only-regex <regex>    use only columns that match a given regex"
    sys.exit(1)

if __name__ == '__main__':
    files = []
    names_file = None
    text_expert = "ngram"
    extension = ".svm"
    ngram_length = 1
    cutoff = 0
    ignore = []
    ignore_regex = None
    only = []
    only_regex = None
    keep_empty_examples = False
    input_dict_file = None
    output_dict_file = None
    no_more_options = False
    i = 1
    while i < len(sys.argv):
        if no_more_options:
            files.append(sys.argv[i])
        elif sys.argv[i] == "--names":
            i += 1
            names_file = sys.argv[i]
        elif sys.argv[i] == "-N":
            i += 1
            text_expert = sys.argv[i]
        elif sys.argv[i] == "-W":
            i += 1
            ngram_length = int(sys.argv[i])
        elif sys.argv[i] == "--cutoff":
            i += 1
            cutoff = int(sys.argv[i])
        elif sys.argv[i] == "--ignore":
            i += 1
            ignore = sys.argv[i].split(",")
        elif sys.argv[i] == "--ignore-regex":
            i += 1
            ignore_regex = sys.argv[i]
        elif sys.argv[i] == "--only":
            i += 1
            only = sys.argv[i].split(",")
        elif sys.argv[i] == "--only-regex":
            i += 1
            only_regex = sys.argv[i]
        elif sys.argv[i] == "--keep-empty-examples":
            keep_empty_examples = True
        elif sys.argv[i] == "-i":
            i += 1
            input_dict_file = sys.argv[i]
        elif sys.argv[i] == "-o":
            i += 1
            output_dict_file = sys.argv[i]
        elif sys.argv[i] == "-e":
            i += 1
            extension = sys.argv[i]
        elif sys.argv[i] == "--":
            no_more_options = True
        elif sys.argv[i].startswith("-"):
            usage()
        else:
            files.append(sys.argv[i])
        i += 1

    if not names_file or len(files) == 0:
        usage()
    feature_count = {}
    names, label_dict = load_names(names_file, ignore, ignore_regex, only, only_regex)

    feature_dict = {}
    if input_dict_file:
        fp = open(input_dict_file)
        for line in fp:
            tokens = line.strip().split()
            feature_dict[tokens[0]] = tokens[1]
        fp.close()
    else:
        for fp in [open(x) for x in files]:
            for line in fp:
                fields = [x.strip() for x in line.strip().split(",")]
                features = []
                for i in xrange(len(fields) - 1):
                    if names[i] == None: continue
                    if names[i][1] == "set":
                        features.append(names[i][0] + ":" + fields[i])
                    elif names[i][1] == "text":
                        if text_expert == "ngram":
                            features.extend([names[i][0] + ":" + x for x in get_ngrams(fields[i].split(), ngram_length)])
                        elif text_expert == "sgram":
                            features.extend([names[i][0] + ":" + x for x in get_sgrams(fields[i].split(), ngram_length)])
                        elif text_expert == "fgram":
                            features.extend([names[i][0] + ":" + x for x in get_fgrams(fields[i].split(), ngram_length)])
                    elif names[i][1] == "continuous":
                        features.append(names[i][0] + ":")
                for feature in features:
                    if feature not in feature_count:
                        feature_count[feature] = 1
                    else:
                        feature_count[feature] += 1
            fp.close()
        for feature, count in sorted(feature_count.items(), lambda x, y: cmp(y[1], x[1])):
            if count >= cutoff:
                feature_dict[feature] = len(feature_dict) + 1
    if output_dict_file:
        print >>sys.stderr, "writing %s" % output_dict_file
        fp = open(output_dict_file, "w")
        for feature, value in sorted(feature_dict.items(), lambda x, y: cmp(x[1], y[1])):
            fp.write("%s %d\n" % (feature, value))
        fp.close()
    for filename in files:
        print >>sys.stderr, "writing %s%s" % (filename, extension)
        fp = open(filename)
        output_fp = open(filename + extension, "w")
        for line in fp:
            fields = [x.strip() for x in line.strip().split(",")]
            features = []
            for i in xrange(len(fields) - 1):
                if names[i] == None: continue
                if names[i][1] == "set":
                    features.append([names[i][0] + ":" + fields[i], 1])
                elif names[i][1] == "text":
                    if text_expert == "ngram":
                        features.extend([[names[i][0] + ":" + x, 1] for x in get_ngrams(fields[i].split(), ngram_length)])
                    elif text_expert == "sgram":
                        features.extend([[names[i][0] + ":" + x, 1] for x in get_sgrams(fields[i].split(), ngram_length)])
                    elif text_expert == "fgram":
                        features.extend([[names[i][0] + ":" + x, 1] for x in get_fgrams(fields[i].split(), ngram_length)])
                elif names[i][1] == "continuous":
                    if fields[i] != "?":
                        features.append([names[i][0] + ":", fields[i]])
            features = [[feature_dict[x[0]], x[1]] for x in features if x[0] in feature_dict]
            unique_features = {}
            for feature in features:
                if feature[0] not in unique_features:
                    unique_features[feature[0]] = float(feature[1])
                else:
                    unique_features[feature[0]] += float(feature[1])
            if len(unique_features) == 0 and not keep_empty_examples: continue
            label = re.sub(r'\.$', '', fields[-1])
            output_fp.write(("%d " % label_dict[label]) + " ".join(["%d:%g" % (x[0],x[1]) for x in sorted(unique_features.items(), lambda x ,y: cmp(x[0], y[0]))]) + "\n")
        fp.close()
        output_fp.close()
