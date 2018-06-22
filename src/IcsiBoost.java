/* Copyright (C) (2009) (Benoit Favre) <favre@icsi.berkeley.edu>

This program is free software; you can redistribute it and/or 
modify it under the terms of the GNU Lesser General Public License 
as published by the Free Software Foundation; either 
version 2 of the License, or (at your option) any later 
version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU Lesser General Public License for more details.

You should have received a copy of the GNU Lesser General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA. */

import java.util.regex.*;
import java.util.*;
import java.io.*;


class IcsiBoost {
    public class Feature {
    }

    public class ContinuousFeature extends Feature {
        double value;
        public ContinuousFeature(String content) {
            value = Double.parseDouble(content);
        }
        public double getValue() {
            return value;
        }
	public String toString() {
	    return String.format("%f", value);
	}

    }

    public class TextFeature extends Feature {
        protected List<String> values;
        public TextFeature() { 
            values = new ArrayList<>();
        }
        public TextFeature(String content) {
            values = new ArrayList<>(1);
            values.add(content);
        }
        public List<String> getValues() {
            return values;
        }
    }

    public class NGramFeature extends TextFeature {
        public NGramFeature(String content, int length) {
            values = new ArrayList<>();
            String[] tokens = content.split(" ");
            for(int i = 0; i < tokens.length; i++) {
                StringBuilder ngram = new StringBuilder();
		// JHE
                //System.out.println("zzzz"+ tokens[i] +" " + i + " " + length);
                for(int j = 0; j < length && i + j < tokens.length; j++) {
                    if (ngram.length() != 0) ngram.append("#");
                    ngram.append(tokens[i + j]);
		values.add(ngram.toString());
                }
            }
        }
	public String toString() {
	    String r = "<" + values + ">";
            return r;
	}
    }

    public class FGramFeature extends TextFeature {
        public FGramFeature(String content, int length) {
            values = new ArrayList<>();
            String[] tokens = content.split(" ");
            for(int i = 0; i < tokens.length - length; i++) {
                StringBuilder ngram = new StringBuilder(tokens[i]);
                for(int j = 0; j < length; j++) {
                    ngram.append("_");
                    ngram.append(tokens[i + j]);
                }
                values.add(ngram.toString());
            }
        }
	public String toString() {
	    String r = "<" + values + ">";
            return r;
	}

    }

    public class SGramFeature extends TextFeature {
        public SGramFeature(String content, int length) {
            values = new ArrayList<>();
            String[] tokens = content.split(" ");
            for(int i = 0; i < tokens.length - length; i++) {
                values.add(tokens[i] + "_" + tokens[i + length]);
            }
        }
	public String toString() {
	    String r = "<" + values + ">";
            return r;
	}

    }

    public class UndefinedFeature extends Feature {
    }

    public class Classifier {
        protected double alpha;
        protected int column;
        protected double c0[];
        protected double c1[];
        protected double c2[];
        public void classify(Feature feature, double[] result){ }
        public String toString() {
            StringBuilder builder = new StringBuilder(alpha + " " + column);
            builder.append("\n");
            if(c0 != null) {
                builder.append("  c0 ");
                for(int i = 0; i < c0.length; i++) {
                    builder.append(c0[i]).append(" ");
                }
                builder.append("\n");
            }
            if(c1 != null) {
                builder.append("  c1 ");
                for(int i = 0; i < c1.length; i++) {
                    builder.append(c1[i]).append(" ");
                }
                builder.append("\n");
            }
            if(c2 != null) {
                builder.append("  c2 ");
                for(int i = 0; i < c2.length; i++) {
                    builder.append(c2[i]).append(" ");
                }
                builder.append("\n");
            }
            return builder.toString();
        }
    }

    public class ThresholdClassifier extends Classifier {
        double threshold;
        public String toString() {
            String output = super.toString();
            return "threshold:" + threshold + ":" + output;
        }

        public ThresholdClassifier(double threshold, int column, double alpha, double[] c0, double[] c1, double[] c2) {
            this.threshold = threshold;
            this.column = column;
            this.alpha = alpha;
            this.c0 = c0;
            this.c1 = c1;
            this.c2 = c2;
        }

        public void classify(Feature feature, double[] result) { 
            if(UndefinedFeature.class.isInstance(feature)) {
                for(int i = 0; i < result.length; i++) {
                    result[i] += c0[i];
                }
            } else {
                double value = ((ContinuousFeature) feature).getValue();
                if(value < threshold) {
                    for(int i = 0; i < result.length; i++) {
                        result[i] += c1[i];
                    }
                } else {
                    for(int i = 0; i < result.length; i++) {
                        result[i] += c2[i];
                    }
                }
            }
        }
    }

    public class TextClassifier extends Classifier {
        String type = "text";
        String token;

        public String toString() {
            String output = super.toString();
            return "text:" + token + ":" + output;
        }

        public TextClassifier(String token, int column, double alpha, double[] c0, double[] c1) {
            this.token = token;
            this.column = column;
            this.alpha = alpha;
            this.c0 = c0;
            this.c1 = c1;
        }

        public void classify(Feature feature, double[] result) {
            if(UndefinedFeature.class.isInstance(feature)) {
                for(int i = 0; i < result.length; i++) {
                    result[i] += c0[i];
                }
            } else {
                List<String> values = ((TextFeature)feature).getValues();
                if(values.contains(token)) {
                    for(int i = 0; i < result.length; i++) {
                        result[i] += c1[i];
                    }
                } else {
                    for(int i = 0; i < result.length; i++) {
                        result[i] += c0[i];
                    }
                }
            }
        }
    }
    public class Model {
        public List<Classifier> classifiers;
        public String[] labels;
        public List<String> types;
        public Map<String, Integer> mapping;
        static final int TYPE_CONTINUOUS = 0;
        static final int TYPE_TEXT = 1;
        static final int TYPE_IGNORE = 1;
        int[] typesAsInt;
        static public final int TYPE_SGRAM = 2;
        static public final int TYPE_FGRAM = 1;
        static public final int TYPE_NGRAM = 0;
        int ngramLength = 0;
        int ngramType = 0;

        public Model(String stem, int ngramLength, int ngramType) throws IOException {
            this.ngramLength = ngramLength;
            this.ngramType = ngramType;
            loadNames(stem + ".names");
            loadShyp(stem + ".shyp");
        }
        public void loadNames(String filename) throws IOException {
            mapping = new HashMap<>();
            types = new ArrayList<>();
            boolean firstLine = true;
            BufferedReader input = new BufferedReader(new FileReader(filename));
            String line;
            while(null != (line = input.readLine())) {
                line = line.trim();
                if(line.matches("^(\\s*|\\s*\\|.*)$")) continue;
                if(firstLine) {
                    labels = line.split("(^\\s+|\\s*,\\s*|\\s*\\.?$)");
                    firstLine = false;
                } else {
                    String result[] = line.split("(^\\s+|\\s*:\\s*|\\s*\\.?$)");
                    //System.err.println("QQQQ " + line + " " + result[1] + " " + result.length);
                    if(result.length >= 2) { // etait == 2
                        mapping.put(result[0], types.size());
			//System.err.println("QQQQ " + line + " " + result[0] + " " + types.size());
                        types.add(result[1]);
                    }
                }
            }
            typesAsInt = new int[types.size()];
            for(int i = 0; i < types.size(); i++) {
                if("ignore".equals(types.get(i))) {
                    typesAsInt[i] = TYPE_IGNORE;
                } else if("continuous".equals(types.get(i))) {
                    typesAsInt[i] = TYPE_CONTINUOUS;
                } else {
                    typesAsInt[i] = TYPE_TEXT;
                }
            }
        }

        public void loadShyp(String filename) throws IOException {
            classifiers = new ArrayList<>();
            boolean seenIterations = false;
            BufferedReader input = new BufferedReader(new FileReader(filename));
            String line;
            double alpha = Double.NaN;
            String name;
            String token = null;
            double threshold;
            double c0[] = null;
            double c1[] = null;
            double c2[] = null;
            int column = -1;
            while(null != (line = input.readLine())) {
                line = line.trim();
                if(line.length() == 0) continue;
                if(!seenIterations) {
                    numIterations = Integer.parseInt(line);
                    seenIterations = true;
                } else {
                    Pattern textPattern = Pattern.compile("^\\s*(\\S+)\\s+Text:SGRAM:([^:]+):(.*?) *$");
                    Matcher textMatcher = textPattern.matcher(line);
                    Pattern thresholdPattern = Pattern.compile("^\\s*(\\S+)\\s+Text:THRESHOLD:([^:]+):");
                    Matcher thresholdMatcher = thresholdPattern.matcher(line);
                    if(textMatcher.matches()) {
                        alpha = Double.parseDouble(textMatcher.group(1));
                        name = textMatcher.group(2);
                        token = textMatcher.group(3);
                        Integer columnInteger = mapping.get(name);
                        if(columnInteger != null) {
                            column = columnInteger;
                            String type = types.get(column);
                            if(type.contains(",")) {
                                String values[] = type.split("\\s*,\\s*");
                                int tokenId = Integer.parseInt(token);
                                if(tokenId < 0 || tokenId >= values.length) {
                                    System.err.println("ERROR: value not found \"" + tokenId + "\" in names file");
                                    System.exit(1);
                                }
                                token = values[Integer.parseInt(token)];
                            }
                        } else {
                            System.err.println("ERROR: name not found \"" + name + "\" in names file");
                            System.exit(1);
                        }
                        c0 = null;
                        c1 = null;
                        c2 = null;
                    } else if(thresholdMatcher.matches()) {
                        alpha = Double.parseDouble(thresholdMatcher.group(1));
                        name = thresholdMatcher.group(2);
                        Integer columnInteger = mapping.get(name);
                        if(columnInteger != null) {
                            column = columnInteger;
                            String type = types.get(column);
                            if(!"continuous".equals(type)) {
                                System.err.println("ERROR: unsupported type \"" + type + "\" for threshold");
                                System.exit(1);
                            }
                        } else {
                            System.err.println("ERROR: name not found \"" + name + "\" in names file");
                            System.exit(1);
                        }
                        c0 = null;
                        c1 = null;
                        c2 = null;
                        token = null;
                    } else {
                        String values[] = line.split(" ");
                        if(values.length == labels.length) {
                            if(c0 == null) {
                                c0 = new double[labels.length];
                                for(int i = 0; i < values.length; i++) {
                                    c0[i] = Double.parseDouble(values[i]);
                                }
                            } else if(c1 == null) {
                                c1 = new double[labels.length];
                                for(int i = 0; i < values.length; i++) {
                                    c1[i] = Double.parseDouble(values[i]);
                                }
                                if(token != null) {
                                    TextClassifier classifier = new TextClassifier(token, column, alpha, c0, c1);
                                    classifiers.add(classifier);
                                }
                            }
                            else {
                                c2 = new double[labels.length];
                                for(int i = 0; i < values.length; i++) {
                                    c2[i] = Double.parseDouble(values[i]);
                                }
                            }
                        } else if(values.length == 1) {
                            threshold = Double.parseDouble(line);
                            ThresholdClassifier classifier = new ThresholdClassifier(threshold, column, alpha, c0, c1, c2);
                            classifiers.add(classifier);
                        } else {
                            System.err.println("ERROR: unexpected line \"" + line + "\" in shyp file");
                            System.exit(1);
                        }
                    }
                }
            }
        }
        public int numIterations;
        public Example readExample(String line) {
            Example example = new Example();
            String fields[] = line.split("\\s*,\\s*");
            if(fields.length == typesAsInt.length || fields.length == typesAsInt.length + 1) {
                for(int i = 0; i < typesAsInt.length; i++) {
                    if("?".equals(fields[i])) {
                        example.features.add(new UndefinedFeature());
                    } else {
                        if(typesAsInt[i] == TYPE_TEXT) {
                            switch (ngramType) {
                                case TYPE_NGRAM:
                                    example.features.add(new NGramFeature(fields[i], ngramLength));
                                    break;
                                case TYPE_FGRAM:
                                    example.features.add(new FGramFeature(fields[i], ngramLength));
                                    break;
                                case TYPE_SGRAM:
                                    example.features.add(new SGramFeature(fields[i], ngramLength));
                                    break;
                                default:
                                    break;
                            }
                        } else if(typesAsInt[i] == TYPE_CONTINUOUS) {
                            example.features.add(new ContinuousFeature(fields[i]));
                        }
                    }
                }
                if(fields.length == typesAsInt.length + 1) {
                    String llabels[] = fields[fields.length - 1].split("(\\s+|\\s*\\.$)");
                    for(int i = 0; i < llabels.length; i++) {
                        example.labels.add(llabels[i]);
                    }
                }
            }
	    //System.err.println("Ex.: " + example);
            return example;
        }
    }
    public class Example {
        public List<Feature> features = new ArrayList<>();
        public List<String> labels = new ArrayList<>();

	// put the results of the evaluation
        public List<Integer> haslabels = new ArrayList<>();
        public List<Double> values = new ArrayList<>();
	
	public String toString() {
	    String r = features + ":" + labels + " " + haslabels + " " + values;
	    return r;
	}
	
	public String results() {
	    StringBuilder sb = new StringBuilder();
	    boolean first = true;
	    for (Integer i : haslabels) {
		if (!first) sb.append(' ');
		else first = false;
		sb.append(i);
	    }
	    for (Double d : values) {
		sb.append(' ');
		sb.append(d);
	    }

	    return sb.toString();
	}
	
    }

    Model model;

    public Example decode(String line) {
        Example example = model.readExample(line);
        double result[] = new double[model.labels.length];
        for(int i = 0; i < model.numIterations; i++) {
            Classifier classifier = model.classifiers.get(i);
            classifier.classify(example.features.get(classifier.column), result);
        }
        for (int i = 0; i < model.labels.length; i++) {
	    //if (i != 0) System.out.print(" ");
            //if (example.labels.contains(model.labels[i])) System.out.print("1");
            //else System.out.print("0");

            if (example.labels.contains(model.labels[i])) example.haslabels.add(1);
            else example.haslabels.add(1);
	    
        }

        for(int i = 0; i < result.length; i++) {
	    //System.out.println("eeeeee " + model.numIterations + " " + result[i]);
	    double x = result[i];
	    double res = 1.0 / (1.0 + Math.exp(-2.0 * x));
	    //System.out.print(" " + res);
	    example.values.add(res);
        }
        //System.out.println();
        //System.out.println("EEE: " + example);
        return example;
    }
    public IcsiBoost(String stem) throws IOException {
        model = new Model(stem, 1, Model.TYPE_NGRAM);
    }
    public static void main(String args[]) {
        try {
            IcsiBoost decoder = new IcsiBoost(args[0]);
            BufferedReader reader = new BufferedReader(new FileReader(args[1]));
            String line;
            while(null != (line = reader.readLine())) {
                Example example = decoder.decode(line);
		System.out.println(example.results());
            }
        } catch (IOException e) {
            e.printStackTrace();
        }
    }
}
