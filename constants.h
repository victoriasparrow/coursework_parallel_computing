#ifndef CONSTANTS_H
#define CONSTANTS_H
#include <filesystem>
#include<string>

namespace constants {
    inline constexpr int threadsNumber{2};
    inline constexpr int locks{256};
    inline std::string initializeStopWords() {
        std::filesystem::path stopWordsFile = "../corpus/stopWords.txt";
        if (std::filesystem::exists(stopWordsFile)) {
            return stopWordsFile.string();
        }
        return{};
    }
    inline const std::string stopWordsFile = initializeStopWords();
    inline const std::filesystem::path corpusPath = "../corpus/";
    //inline const std::filesystem::path corpusPath = "../gutenbergfineweb/";
}

#endif //CONSTANTS_H