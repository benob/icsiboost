#include "IcsiBoost.h"

#include <sstream>
#include <iostream>
#include <fstream>
#include <typeinfo>

namespace icsiboost {

    static void Split(const std::string& text, std::vector<std::string> &tokens, const std::string &delim = " ", bool aggregate = true) {
        std::string::size_type lastPos = 0;
        if(aggregate) lastPos = text.find_first_not_of(delim, 0);
        std::string::size_type pos = text.find_first_of(delim, lastPos);
        tokens.clear();
        while (std::string::npos != pos || std::string::npos != lastPos)
        {
            tokens.push_back(text.substr(lastPos, pos - lastPos));
            lastPos = text.find_first_not_of(delim, pos);
            if(!aggregate && lastPos != std::string::npos) lastPos = pos + 1;
            pos = text.find_first_of(delim, lastPos);
        }
    }

    static std::string Strip(const std::string &text, const std::string &delim = " ") {
        std::string::size_type start = text.find_first_not_of(delim);
        std::string::size_type end = text.find_last_not_of(delim);
        if(start != std::string::npos) return text.substr(start, end + 1);
        return text; 
    }

    static std::string Chomp(const std::string &text, const std::string &delim = " ") {
        std::string::size_type end = text.find_last_not_of(delim);
        if(end != std::string::npos) return text.substr(0, end + 1);
        return text; 
    }

    static bool StartsWith(const std::string &text, const std::string& key) {
        return text.substr(0, key.length()) == key;
    }

    ContinuousFeature::ContinuousFeature(const std::string& text) : value(0), unknown(false) {
        if(text == "?") {
            unknown = true;
        } else {
            std::stringstream str(text);
            str >> value;
        }
    }

    TextFeature::TextFeature(const std::string& text) {
        values.insert(text);
    }

    NGramFeature::NGramFeature(const std::string& text, size_t length) {
        std::vector<std::string> tokens;
        Split(text, tokens);
        for(size_t i = 0; i < tokens.size(); i++) {
            std::string ngram = tokens[i];
            values.insert(ngram);
            for(size_t j = 1; j < length && i + j < tokens.size(); j++) {
                ngram += "#";
                ngram += tokens[i + j];
                values.insert(ngram);
            }
        }
    }

    FGramFeature::FGramFeature(const std::string& text, size_t length) {
        std::vector<std::string> tokens;
        Split(text, tokens);
        for(size_t i = 0; i < tokens.size(); i++) {
            std::string ngram = tokens[i];
            for(size_t j = 1; j < length && i + j < tokens.size(); j++) {
                ngram += "#";
                ngram += tokens[i + j];
            }
            values.insert(ngram);
        }
    }

    SGramFeature::SGramFeature(const std::string& text, size_t length) {
        std::vector<std::string> tokens;
        Split(text, tokens);
        for(size_t i = 0; i < tokens.size() - length; i++) {
            std::string ngram = tokens[i] + "#" + tokens[i + length];
            values.insert(ngram);
        }
    }

    void ThresholdClassifier::Classify(const Feature* _feature, std::vector<double>& result) const {
        const ContinuousFeature* feature = dynamic_cast<const ContinuousFeature*>(_feature);
        if(feature->IsUnknown()) {
            for(size_t i = 0; i < result.size(); i++) {
                result[i] += c0[i];
            }
        } else {
            if(feature->GetValue() < threshold) {
                for(size_t i = 0; i < result.size(); i++) {
                    result[i] += c1[i];
                }
            } else {
                for(size_t i = 0; i < result.size(); i++) {
                    result[i] += c2[i];
                }
            }
        }
    }

    void TextClassifier::Classify(const Feature* _feature, std::vector<double>& result) const {
        const TextFeature* feature = dynamic_cast<const TextFeature*>(_feature);
        if(feature->MatchesValue(token)) {
            for(size_t i = 0; i < result.size(); i++) {
                result[i] += c1[i];
            }
        } else {
            for(size_t i = 0; i < result.size(); i++) {
                result[i] += c0[i];
            }
        }
    }

    Model::Model(const std::string& stem, int _ngramLength, int _ngramType) : ngramLength(_ngramLength), ngramType(_ngramType), numIterations(0), loaded(false) {

        LoadNames(stem + ".names");
        LoadShyp(stem + ".shyp");
    }

    void Model::LoadNames(const std::string& filename) {
        mapping.clear();
        types.clear();
        labels.clear();
        bool firstLine = true;
        std::string line;
        std::ifstream input(filename.c_str());
        while(std::getline(input, line)) {
            line = Strip(line);
            if(line == "" || StartsWith(line, "|")) continue; // comment or empty line
            if(firstLine) {
                line = Chomp(line, ".");
                Split(line, labels, " ,");
                firstLine = false;
            } else {
                std::vector<std::string> tokens;
                line = Chomp(line, ".");
                Split(line, tokens, " :");
                if(tokens.size() == 2) {
                    mapping[tokens[0]] = types.size();
                    if(tokens[1] == "ignore") columnTypes.push_back(TYPE_IGNORE);
                    else if(tokens[1] == "continuous") columnTypes.push_back(TYPE_CONTINUOUS);
                    else if(tokens[1] == "text") columnTypes.push_back(TYPE_TEXT);
                    else columnTypes.push_back(TYPE_SET);
                    types.push_back(tokens[1]); // keep type for sets of values
                }
            }
        }
    }

    void Model::LoadShyp(const std::string& filename) {
        bool seenIterations = false;
        std::ifstream input(filename.c_str());
        std::string line;
        double alpha = 0;
        std::string name;
        std::string token = "";
        double threshold = 0;
        std::vector<double> c0;
        std::vector<double> c1;
        std::vector<double> c2;
        size_t column = -1;
        while(std::getline(input, line)) {
            line = Strip(line);
            if(line == "") continue;
            if(!seenIterations) {
                std::stringstream str(line);
                str >> numIterations;
                seenIterations = true;
            } else {
                std::vector<std::string> tokens;
                Split(line, tokens);
                if(tokens.size() > 1 && StartsWith(tokens[1], "Text:SGRAM:")) {
                    std::stringstream str(tokens[0]);
                    str >> alpha;
                    std::string::size_type nameEnd = tokens[1].find(":", 11);
                    name = tokens[1].substr(11, nameEnd - 11);
                    token = tokens[1].substr(nameEnd + 1);
                    std::tr1::unordered_map<std::string, int>::const_iterator found = mapping.find(name);
                    if(found == mapping.end()) {
                        std::cerr << "ERROR: column \"" << name << "\" not found in names file\n";
                        return;
                    }
                    column = found->second;
                    if(columnTypes[column] == TYPE_SET) {
                        std::vector<std::string> values;
                        Split(types[column], values);
                        std::stringstream str2(token);
                        int tokenId;
                        str2 >> tokenId;
                        if(tokenId < 0 || tokenId >= (int) values.size()) {
                            std::cerr << "ERROR: value not found \"" << tokenId << "\" in names file (" << line << ")\n";
                        } else {
                            token = values[tokenId];
                        }
                    }
                    c0.clear();
                    c1.clear();
                    c2.clear();
                } else if(tokens.size() > 1 && StartsWith(tokens[1], "Text:THRESHOLD:")) {
                    std::stringstream str(tokens[0]);
                    str >> alpha;
                    std::string::size_type nameEnd = tokens[1].find(":", 15);
                    name = tokens[1].substr(15, nameEnd - 15);
                    std::tr1::unordered_map<std::string, int>::const_iterator found = mapping.find(name);
                    if(found == mapping.end()) {
                        std::cerr << "ERROR: column \"" << name << "\" not found in names file\n";
                        return;
                    }
                    column = found->second;
                    c0.clear();
                    c1.clear();
                    c2.clear();
                    token = "";
                } else {
                    if(tokens.size() == labels.size()) {
                        if(c0.size() == 0) {
                            for(size_t i = 0; i < tokens.size(); i++) {
                                std::stringstream str(tokens[i]);
                                double value;
                                str >> value;
                                c0.push_back(value);
                            }
                        } else if(c1.size() == 0) {
                            for(size_t i = 0; i < tokens.size(); i++) {
                                std::stringstream str(tokens[i]);
                                double value;
                                str >> value;
                                c1.push_back(value);
                            }
                            if(token != "") {
                                //TextClassifier classifier(token, column, alpha, c0, c1);
                                classifiers.push_back(new TextClassifier(token, column, alpha, c0, c1));
                            }
                        }
                        else {
                            for(size_t i = 0; i < tokens.size(); i++) {
                                std::stringstream str(tokens[i]);
                                double value;
                                str >> value;
                                c2.push_back(value);
                            }
                        }
                    } else if(tokens.size() == 1) {
                        std::stringstream str(tokens[0]);
                        str >> threshold;
                        //ThresholdClassifier classifier(threshold, column, alpha, c0, c1, c2);
                        classifiers.push_back(new ThresholdClassifier(threshold, column, alpha, c0, c1, c2));
                    } else {
                        std::cerr << "ERROR: unexpected line \"" << line << "\" in shyp file\n";
                        return;
                    }
                }
            }
        }
    }

    bool Model::ReadExample(const std::string& line, Example& output) const {
        std::vector<std::string> fields;
        Split(line, fields, ",", false);
        if(fields.size() == columnTypes.size() || fields.size() == columnTypes.size() + 1) {
            for(size_t i = 0; i < fields.size(); i++) {
                std::string field = Strip(fields[i]);
                if(columnTypes[i] == TYPE_CONTINUOUS) {
                    output.AddFeature(new ContinuousFeature(field));
                } else if(columnTypes[i] == TYPE_TEXT) {
                    if(ngramType == TEXT_TYPE_NGRAM) output.AddFeature(new NGramFeature(field, ngramLength));
                    else if(ngramType == TEXT_TYPE_FGRAM) output.AddFeature(new FGramFeature(field, ngramLength));
                    else if(ngramType == TEXT_TYPE_SGRAM) output.AddFeature(new SGramFeature(field, ngramLength));
                } else if(columnTypes[i] == TYPE_SET) {
                    output.AddFeature(new TextFeature(field));
                } else {
                    output.AddFeature(NULL);
                }
            }
        } else {
            std::cerr << "ERROR: unable to parse example \"" << line << "\" (" << fields.size() << " <> " << columnTypes.size() << "\n";
            return false;
        }
        return true;
    }

    std::string Model::Classify(const Example& example, std::vector<double> &scores) const {
        scores.resize(labels.size());
        for(int i = 0; i < numIterations; i++) {
            const Classifier *classifier = classifiers[i];
            classifier->Classify(example.GetFeature(classifier->GetColumn()), scores);
        }
        double max = 0;
        int argmax = -1; 
        for(size_t i = 0; i < labels.size(); i++) {
            scores[i] /= numIterations;
            if(scores[i] > max || argmax == -1) {
                max = scores[i];
                argmax = i;
            }
        }
        return labels[argmax];
    }
}
