#include <iostream>
#include <string>
#include <fstream>
#include <sstream>
#include <unordered_set>

#include "constants.h"
#include "TextParser.h"

std::string txtparcer::createLines(std::string& fileName, std::vector<uint32_t> positions) {
    if (positions.empty()) return "";
    std::string results;
    std::string filePath = constants::corpusPath / fileName;
    std::ifstream file;
    file.open(filePath);
    if (!file.is_open()) {
        std::cerr << "createLines: failed to open file " << filePath << std::endl;
        return "";
    }
    std::string line;
    uint32_t currentWordCount = 1;
    uint32_t linesFound = 0;
    auto iterator = positions.begin();

    while (std::getline(file, line) && linesFound < constants::kMaxLines) {
        if (iterator == positions.end()) {  // have found all the positions in the doc
            break;
        }
        std::size_t current = 0;

        while (true) {
            std::size_t start = line.find_first_not_of(constants::delims, current);
            if (start == std::string::npos) break;

            std::size_t end = line.find_first_of(constants::delims, start);
            std::size_t length = 0;
            if (end == std::string::npos) {
                length = line.length() - start;
            } else { length = end - start; }

            if (length > constants::kMaxWordLength || length <= 2) {
                currentWordCount++;
                current = end;
                if (current == std::string::npos) { break; }
                continue;
            }
            while (iterator != positions.end() && *iterator == currentWordCount) {
                constexpr int kContextWindow = 30;
                size_t contextStart = 0, contextEnd = 0;
                if (start > kContextWindow) {
                    contextStart = start - kContextWindow;
                }
                else { start = 0; }
                contextEnd = start + length + kContextWindow;
                if (contextEnd > line.length()) contextEnd = line.length();

                std::string subString = line.substr(contextStart, contextEnd - contextStart);
                results += subString + "\n";
                linesFound++;
                ++iterator;
            }

            currentWordCount++;

            current = end;
            if (current == std::string::npos) break;
        }
    }
    return results;
}

void txtparcer::processLine(const std::string_view line, std::unordered_map<std::string, std::vector<uint32_t>>& pos, const std::unordered_set<std::string, stringHash, std::equal_to<>>& stopWordsMap, uint32_t& wordCount) {
    std::size_t kMaxWordLength = constants::kMaxWordLength;
    std::string_view delims = constants::delims;
    std::size_t current = 0;

    while (true) {
        std::size_t start = line.find_first_not_of(delims, current);
        if (start == std::string::npos) {
            break;
        }

        std::size_t end = line.find_first_of(delims, start);
        std::size_t length = 0;
        if (end == std::string::npos) {
            length = line.length() - start;
        } else { length = end - start; }

        if (length > kMaxWordLength || length <= 2) {
            wordCount++;
            current = end;
            if (current == std::string::npos) { break; }
            continue;
        }
        std::string_view tokenTemp(line.data() + start, length);

        if (!stopWordsMap.contains(tokenTemp) && tokenTemp.find("--") == std::string_view::npos) { // no temp memory allocated for look up
            pos[std::string(tokenTemp)].push_back(wordCount);
        }
        wordCount++;

        current = end;
        if (current == std::string::npos) {
            break;
        }
    }
}

void txtparcer::loadStopWords(std::unordered_set<std::string, stringHash, std::equal_to<>>& stopWordsMap) {
    std::ifstream file;
    file.open(constants::stopWordsFile);
    if (!file.is_open()) {
        return;
    }
    std::string line;
    while (std::getline(file, line)) {
        stopWordsMap.insert(line);
    }
    file.close();
}

void txtparcer::processDocument(const std::string& fileName, std::unordered_map<std::string, std::vector<uint32_t>>& pos, const std::unordered_set<std::string, stringHash, std::equal_to<>>& stopWordsMap) {
    static std::size_t allUniqueWords = 0;
    std::ifstream file;
    std::string line;
    std::string filePath = constants::corpusPath / fileName;
    //std::cout << "filepath looks like " << filePath << std::endl;
    file.open(filePath);
    if (file.is_open()) {
        uint32_t wordCountDoc = 1;
        while (std::getline(file, line )) {
            for (char &toLower : line) {
                if( toLower >= 'A' && toLower <= 'Z' ) {
                    toLower += 'a' - 'A';
                }
            }
            txtparcer::processLine(line, pos, stopWordsMap, wordCountDoc);
        }
    }
    else {
        std::cerr << "File failed to open." << std::endl;
    }
    file.close();
    allUniqueWords += pos.size();
    //std::cout << "allUniqueWords " << allUniqueWords << std::endl;
    //std::cout << "unique words in local hash map: " << pos.size() << std::endl;
}

std::vector<std::string> txtparcer::booleanQuery(const std::string& query) {
    std::vector<std::string> tokens;
    std::istringstream iss(query);
    std::string word;
    while (iss >> word) {
        if (word != "and") {
            tokens.push_back(word);
        }
    }
    return tokens;
}