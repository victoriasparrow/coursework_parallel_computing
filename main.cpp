#include <iostream>
#include <string_view>
#include <fstream>
#include <unordered_set>
#include <cstddef>
#include "constants.h"
#include "HashTable.h"

// void* operator new(std::size_t sz){
//     std::cout << " allocating: " << sz << '\n';
//     return std::malloc(sz);
// }

std::vector<std::filesystem::path> getFilesNamesInDirectory(const std::filesystem::path& path) {
    std::vector<std::filesystem::path> fileNamePaths;
    if (!std::filesystem::exists(path)) {
        return fileNamePaths;
    }
    for (const auto& entry : std::filesystem::directory_iterator(path)) {
        std::cout << entry.path().string() << std::endl;
        fileNamePaths.push_back(entry);
    }
    return fileNamePaths;
}

struct stringHash {
    using hash_type = std::hash<std::string_view>;
    using is_transparent = void;
    std::size_t operator()(const char* str) const{
        return hash_type{}(str);
    }
    std::size_t operator()(std::string_view str) const{
        return hash_type{}(str);
    }
    std::size_t operator()(const std::string& str) const{
        return hash_type{}(str);
    }
};

void loadStopWords(std::unordered_set<std::string, stringHash, std::equal_to<>>& stopWordsMap) {
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

void processLine(const std::string_view line, std::unordered_map<std::string, std::vector<uint32_t>>& pos, const std::unordered_set<std::string, stringHash, std::equal_to<>>& stopWordsMap, uint32_t& wordCount) {
    std::size_t kMaxWordLength = 40;
    constexpr std::string_view delims = " \t\n\r\v\f_/!.,@#$%^&*();:?{}<>|`~[]\"";
    std::size_t current = 0;

    while (true) {
        std::size_t start = line.find_first_not_of(delims, current);
        if (start == std::string::npos) {
            break;
        }

        std::size_t end = line.find_first_of(delims, start);

        std::size_t length = (end == std::string::npos) ? line.length() - start : end - start;

        if (length > kMaxWordLength) {
            current = end;
            if (current == std::string::npos) {
                break;
            }
            continue;
        }

        std::string_view tokenTemp(line.data() + start, length);
        if (tokenTemp.length() >= 2 && tokenTemp.substr(tokenTemp.length() - 2) == "'s") {
            tokenTemp.remove_suffix(2);
        }
        if (!stopWordsMap.contains(tokenTemp)) { // no temp memory allocated for look up
            pos[std::string(tokenTemp)].push_back(wordCount);
        }
        wordCount++;
        current = end;
        if (current == std::string::npos) {
            break;
        }
    }
}

void processDocument(std::string_view fileName, std::unordered_map<std::string, std::vector<uint32_t>>& pos, const std::unordered_set<std::string, stringHash, std::equal_to<>>& stopWordsMap) {
    std::ifstream file;
    std::string line;
    file.open(fileName);
    if (file.is_open()) {
        uint32_t wordCountDoc = 1;
        while (std::getline(file, line )) {
            std::transform(line.begin(), line.end(), line.begin(), ::tolower);
            processLine(line, pos, stopWordsMap, wordCountDoc);
        }
    }
    file.close();
    //std::cout << "wordPositions.size() " << pos.size() << std::endl;
}

uint32_t getTokenID(const std::string &word, std::unordered_map<std::string, uint32_t> &finaltokenToID, uint32_t& nextID) {
    if (finaltokenToID.find(word) == finaltokenToID.end()) {
        finaltokenToID[word] = nextID++;
    }
    return finaltokenToID.at(word);
}

void testHashMap() {
    std::unordered_map<std::string, std::vector<uint32_t>> wordPositionsForMyTable; // local hashMap for each thread
    std::unordered_map<std::string, std::vector<uint32_t>> wordPositionsForSTL; // duplicate because we do std::move
    std::unordered_set<std::string, stringHash, std::equal_to<>> stopWordsMap;
    std::unordered_map<std::string, uint32_t> tokenToID;
    uint32_t nextID = 0;
    loadStopWords(stopWordsMap);

    std::string fileName = "../corpus/3497.txt";
    processDocument(fileName, wordPositionsForMyTable, stopWordsMap);
    wordPositionsForSTL = wordPositionsForMyTable;

    HashTable myHashMap;
    std::unordered_map<uint32_t, std::vector<Posting>> stdHashMap;

    auto execStartMyTable = std::chrono::steady_clock::now();
    for (std::pair<const std::string, std::vector<uint32_t>>& pair : wordPositionsForMyTable){
        const std::string& word = pair.first;
        Posting thisDoc (1, std::move(pair.second));
        std::vector<Posting> postings;

        postings.push_back(thisDoc);

        uint32_t id = getTokenID(word, tokenToID, nextID);
        myHashMap.insert(id, std::move(postings));
    }

    int myInsertSize = myHashMap.size();
    int i = 0;
    for (std::pair<const std::string, std::vector<uint32_t>>& pair : wordPositionsForMyTable){
        const std::string& word = pair.first;
        uint32_t id = getTokenID(word, tokenToID, nextID);
        myHashMap.erase(id);
        i++;
        if (i > 1000) break;
    }
    int myEraseSize = myHashMap.size();
    int myFoundAmount = 0;

    for (std::pair<const std::string, std::vector<uint32_t>>& pair : wordPositionsForMyTable){
        const std::string& word = pair.first;
        uint32_t id = getTokenID(word, tokenToID, nextID);
        if (myHashMap.findByKey(id) != nullptr) {
            myFoundAmount++;
        }
    }
    auto execEndMyTable = std::chrono::steady_clock::now();
    auto execDurationMyTable = std::chrono::duration_cast<std::chrono::microseconds>(execEndMyTable - execStartMyTable).count();

    auto execStartStd = std::chrono::steady_clock::now();
    for (std::pair<const std::string, std::vector<uint32_t>>& pair : wordPositionsForSTL){
        const std::string& word = pair.first;
        Posting thisDoc (1, std::move(pair.second));
        std::vector<Posting> postings;
        postings.push_back(thisDoc);
        uint32_t id = getTokenID(word, tokenToID, nextID);
        stdHashMap.insert({id, postings});
    }
    std::size_t stdInsertSize = stdHashMap.size();
    int j = 0;
    for (std::pair<const std::string, std::vector<uint32_t>>& pair : wordPositionsForSTL){
        const std::string& word = pair.first;
        uint32_t id = getTokenID(word, tokenToID, nextID);
        stdHashMap.erase(id);
        j++;
        if (j > 1000) break;
    }

    std::size_t stdEraseSize = stdHashMap.size();
    int stdFoundAmount = 0;

    for (std::pair<const std::string, std::vector<uint32_t>>& pair : wordPositionsForSTL){
        const std::string& word = pair.first;
        uint32_t id = getTokenID(word, tokenToID, nextID);
        if (stdHashMap.find(id) != stdHashMap.end()) {
            stdFoundAmount++;
        }
    }
    auto execEndStd = std::chrono::steady_clock::now();
    auto execDurationStd = std::chrono::duration_cast<std::chrono::microseconds>(execEndStd - execStartStd).count();

    std::cout << "My HashTable:" << std::endl;
    std::cout << "Time: " << execDurationMyTable << ", after insert: " << myInsertSize << ", after erase: " << myEraseSize << ", found: " << myFoundAmount << std::endl;
    std::cout << "STL unordered_map:" << std::endl;
    std::cout << "Time: " << execDurationStd << ", after insert: " << stdInsertSize << ", after erase: " << stdEraseSize << ", found: " << stdFoundAmount << std::endl << std::endl;

    if (myInsertSize == stdInsertSize && myEraseSize == stdEraseSize && myFoundAmount == stdFoundAmount){
        std::cout << "Something is correct, thank god" << std::endl;
    }
    else {
        std::cerr << ":(" << std::endl;
    }
}

int main() {
    testHashMap();
}