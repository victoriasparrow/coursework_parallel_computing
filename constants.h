#ifndef CONSTANTS_H
#define CONSTANTS_H
#include <filesystem>
#include <string>
#include <string_view>

namespace constants {
    inline constexpr int threadsNumber{4};
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
    constexpr std::size_t kMaxWordLength = 40;
    constexpr std::size_t kMaxLines = 5;
    constexpr std::string_view delims = " \t\n\r\v\f1234567890_/!.,@#$%^&*();:?{}<>|`~[]+=\"\\-'";
}

#endif //CONSTANTS_H