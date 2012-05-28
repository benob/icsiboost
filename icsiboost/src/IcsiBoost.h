#pragma once

#include <string>
#include <vector>
#include <tr1/unordered_set>
#include <tr1/unordered_map>
#include <iostream>

namespace icsiboost {

    class Feature {
        private:
            int dummy;
        public:
            virtual double GetValue() const { return 0; }
            virtual ~Feature() { }
    };

    class ContinuousFeature : public Feature {
        protected:
            double value;
            bool unknown;
        public:
            ContinuousFeature(const std::string& text);
            virtual ~ContinuousFeature() { }
            double GetValue() const { return value; }
            bool IsUnknown() const { return unknown; }
    };

    class TextFeature : public Feature {
        protected:
            std::tr1::unordered_set<std::string> values;
        public:
            TextFeature() { }
            TextFeature(const std::string& text);
            virtual ~TextFeature() { }
            bool MatchesValue(const std::string& text) const {
                std::tr1::unordered_set<std::string>::const_iterator found = values.find(text);
                return found != values.end();
            }
    };

    class NGramFeature : public TextFeature {
        public:
            NGramFeature(const std::string& text, size_t length);
    };

    class FGramFeature : public TextFeature {
        public:
            FGramFeature(const std::string& text, size_t length);
    };

    class SGramFeature : public TextFeature {
        public:
            SGramFeature(const std::string& text, size_t length);
    };

    class Classifier {
        protected:
            double alpha;
            int column;
            std::vector<double> c0;
            std::vector<double> c1;
            std::vector<double> c2;
        public:
            Classifier() : alpha(0), column(0) { }
            virtual ~Classifier() { }
            Classifier(int _column, double _alpha, const std::vector<double> &_c0, const std::vector<double> &_c1, const std::vector<double> &_c2 = std::vector<double>()) : alpha(_alpha), column(_column), c0(_c0), c1(_c1), c2(_c2) { }
            virtual void Classify(const Feature* feature, std::vector<double>& result) const { 
                std::cerr << "ERROR: should not be called\n";
            }
            int GetColumn() const { return column; }
    };

    class ThresholdClassifier : public Classifier {
        protected:
            double threshold;
        public:
            ThresholdClassifier(double _threshold, int _column, double _alpha, const std::vector<double> &_c0, const std::vector<double> &_c1, const std::vector<double> &_c2) : Classifier(_column, _alpha, _c0, _c1, _c2), threshold(_threshold) { }
            virtual void Classify(const Feature* feature, std::vector<double>& result) const ;
    };

    class TextClassifier : public Classifier {
        protected:
            std::string token;
        public:
            TextClassifier(const std::string &_token, int _column, double _alpha, const std::vector<double> &_c0, const std::vector<double> &_c1) : Classifier(_column, _alpha, _c0, _c1), token(_token) { }

            virtual void Classify(const Feature* feature, std::vector<double>& result) const;
    };

    class Example {
        protected:
            std::vector<const Feature*> features;
        public:
            Example() { }
            void AddFeature(const Feature* feature) {
                features.push_back(feature);
            }
            const Feature* GetFeature(int column) const {
                return features[column];
            }
            ~Example() {
                for(std::vector<const Feature*>::const_iterator i = features.begin(); i != features.end(); i++)
                    delete *i;
            }
    };

    static const int TYPE_IGNORE = 0;
    static const int TYPE_CONTINUOUS = 1;
    static const int TYPE_TEXT = 2;
    static const int TYPE_SET = 3;

    static const int TEXT_TYPE_NGRAM = 0;
    static const int TEXT_TYPE_SGRAM = 1;
    static const int TEXT_TYPE_FGRAM = 2;

    class Model {
        protected:
            std::vector<Classifier*> classifiers;
            std::vector<std::string> labels;
            std::vector<std::string> types; 
            std::vector<int> columnTypes;
            std::tr1::unordered_map<std::string, int> mapping;
            int ngramLength;
            int ngramType;
            int numIterations;
            bool loaded;

            void LoadNames(const std::string& filename);
            void LoadShyp(const std::string& filename);

        public:
            Model(const std::string& stem, int _ngramLength = 1, int _ngramType = TEXT_TYPE_NGRAM);
            ~Model() {
                for(std::vector<Classifier*>::iterator i = classifiers.begin(); i != classifiers.end(); i++)
                    delete *i;
            }
            bool IsLoaded() const { return loaded; }
            bool ReadExample(const std::string& line, Example& output) const;
            std::string Classify(const Example& example, std::vector<double> &scores) const;
    };

}
