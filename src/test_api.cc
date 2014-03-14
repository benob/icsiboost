#include "IcsiBoost.h"

#include <iostream>

int main(int argc, char** argv) {
    if(argc != 2) {
        std::cerr << "usage: " << argv[0] << " <stem>\n";
        return 1;
    }
    icsiboost::Model model(argv[1], 3, icsiboost::TEXT_TYPE_NGRAM);
    std::string line;
    while(std::getline(std::cin, line)) {
        icsiboost::Example example;
        model.ReadExample(line, example);
        std::vector<double> scores;
        model.Classify(example, scores);
        for(size_t i = 0; i < scores.size(); i++) 
            std::cout << scores[i] << " ";
        std::cout << "\n";
    }
    return 0;
}
