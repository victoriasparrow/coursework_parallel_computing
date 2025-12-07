#ifndef CONSTANTS_H
#define CONSTANTS_H
#include<string>

namespace constants {
    inline constexpr int threadsNumber{1};
    inline std::string initializeStopWords() {
        std::filesystem::path stopWordsFile = "../stopWords.txt";
        if (std::filesystem::exists(stopWordsFile)) {
            return stopWordsFile.string();
        }
        return{};
    }
    inline const std::string stopWordsFile = initializeStopWords();
    inline const std::filesystem::path corpusPath = "../corpus/";
}

#endif //CONSTANTS_H