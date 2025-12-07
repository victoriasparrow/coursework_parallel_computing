#include <iostream>
#include <string_view>
#include <fstream>
#include <unordered_set>
#include <cstddef>
#include "HashMap.h"
#include "constants.h"

// void* operator new(std::size_t sz){
//     std::cout << " allocating: " << sz << '\n';
//     return std::malloc(sz);
// }

int getFilesNamesInDirectory(const std::filesystem::path& path, HashMap<std::string, uint32_t>& docToID) {
    uint32_t nextDocID = 1;
    if (!std::filesystem::exists(path)) {
        return 0;
    }
    for (const auto& entry : std::filesystem::directory_iterator(path)) {
        if (entry.path().extension().string() == ".txt") {
            //std::cout << entry.path().filename().string() << std::endl;
            docToID.insert(entry.path().string(), nextDocID);
            nextDocID++;
        }
    }
    return nextDocID;
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
        std::size_t length = 0;
        if (end == std::string::npos) {
            length = line.length() - start;
        } else { length = end - start; }

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
    //std::cout << "unique words in local hash map: " << pos.size() << std::endl;
}

uint32_t getTokenID(const std::string &word, HashMap<std::string, uint32_t> &finaltokenToID, uint32_t& nextID) {
    uint32_t id = finaltokenToID.findByCopy(word);
    if (id == 0) {
        finaltokenToID.insert(word, nextID++);
    }
    return finaltokenToID.findByCopy(word);
}

void testHashMap() {
    std::unordered_map<std::string, int> uniqueWordsMyTable;
    std::unordered_map<std::string, int> uniqueWordsSTL;
    std::unordered_map<std::string, std::vector<uint32_t>> wordPositionsForMyTable; // local hashMap for each thread
    std::unordered_map<std::string, std::vector<uint32_t>> wordPositionsForSTL; // duplicate because we do std::move
    std::unordered_set<std::string, stringHash, std::equal_to<>> stopWordsMap;

    HashMap<std::string, uint32_t> tokenToID;
    HashMap<std::string, uint32_t> docToID;

    HashMap<uint32_t,std::vector<Posting>> myHashMap;
    std::unordered_map<uint32_t, std::vector<Posting>> stdHashMap;

    uint32_t nextID = 1;
    loadStopWords(stopWordsMap);
    getFilesNamesInDirectory(constants::corpusPath, docToID);
    std::vector<std::pair<std::string, uint32_t>> copyDocID = docToID.iteratorCopy();
    std::cout << "docToID Hash Map:" << std::endl;

    auto execStartMyTable = std::chrono::steady_clock::now();
    for (const auto &pair : copyDocID) {
        std::cout << "key: " << pair.first << " ";
        std::cout << "id: " << pair.second << std::endl;
        processDocument(pair.first, wordPositionsForMyTable, stopWordsMap);
        for (std::pair<const std::string, std::vector<uint32_t>>& it : wordPositionsForMyTable){
            uniqueWordsMyTable.insert({it.first, 1});
            const std::string& word = it.first;
            uint32_t id = getTokenID(word, tokenToID, nextID);
            Posting thisDoc (pair.second, std::move(it.second));
            std::vector<Posting> postings;
            postings.push_back(thisDoc);
            myHashMap.insert(std::move(id), std::move(postings));
        }
        wordPositionsForMyTable.clear();
    }
    //std::cout << "There are " << uniqueWordsMyTable.size() << " unique words" << std::endl;
    int myInsertSize = myHashMap.size();
    int i = 0;
    for (std::pair<const std::string, int>& pair : uniqueWordsMyTable){
        const std::string& word = pair.first;
        uint32_t id = getTokenID(word, tokenToID, nextID);
        myHashMap.erase(id);
        i++;
        if (i > 10000) break;
    }
    int myEraseSize = myHashMap.size();
    int myFoundAmount = 0;

    for (std::pair<const std::string, int>& pair : uniqueWordsMyTable){
        const std::string& word = pair.first;
        uint32_t id = getTokenID(word, tokenToID, nextID);
        if (myHashMap.findByKey(id) != nullptr) {
            myFoundAmount++;
        }
    }
    auto execEndMyTable = std::chrono::steady_clock::now();
    auto execDurationMyTable = std::chrono::duration_cast<std::chrono::microseconds>(execEndMyTable - execStartMyTable).count();

    auto execStartStd = std::chrono::steady_clock::now();
    for (const auto &pair : copyDocID) {
        processDocument(pair.first, wordPositionsForSTL, stopWordsMap);
        for (std::pair<const std::string, std::vector<uint32_t>>& it : wordPositionsForSTL){
            uniqueWordsSTL.insert({it.first, 1});
            const std::string& word = it.first;
            uint32_t id = getTokenID(word, tokenToID, nextID);
            Posting thisDoc (pair.second, std::move(it.second));
            stdHashMap[id].push_back(std::move(thisDoc));
        }
        wordPositionsForSTL.clear();
    }
    //std::cout << "There are " << uniqueWordsSTL.size() << " unique words." << std::endl;
    std::size_t stdInsertSize = stdHashMap.size();
    int j = 0;
    for (std::pair<const std::string, int>& pair : uniqueWordsSTL){
        const std::string& word = pair.first;
        uint32_t id = getTokenID(word, tokenToID, nextID);
        stdHashMap.erase(id);
        j++;
        if (j > 10000) break;
    }

    std::size_t stdEraseSize = stdHashMap.size();
    int stdFoundAmount = 0;

    for (std::pair<const std::string, int>& pair : uniqueWordsSTL){
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
        std::cout << "The template passed the test. " << std::endl;
    }
    else {
        std::cerr << ":(" << std::endl;
    }

    int iterations = 0;
    // int correct = 0;
    // int deletedMyTable = 0;
    // int deletedSTL = 0;
    std::vector<std::string> words = {"spectacular", "amazing", "art", "beautiful", "life", "nostalgia", "school", "fun"};
    for (const std::string &pair: words) {
        std::string word = pair;
        std::cout << "\n・ ✦ ・ The token is " << pair << " ・ ✦ ・" << std::endl;
        std::vector<uint32_t> positionsSTL;
        std::vector<uint32_t> positionsMyTable;
        std::cout << "Result in STL hash table: " << std::endl;
        if (stdHashMap.find(tokenToID.findByCopy(word)) != stdHashMap.end()) {
            for (std::size_t m = 0; m < stdHashMap.at(tokenToID.findByCopy(word)).size(); ++m) {
                std::cout << "docID " << stdHashMap.at(tokenToID.findByCopy(word)).at(m).documentID << " ";
                std::cout << "[ ";
                for (int p = 0; p < stdHashMap.at(tokenToID.findByCopy(word)).at(m).positions.size(); p++) {
                    std::cout << stdHashMap.at(tokenToID.findByCopy(word)).at(m).positions.at(p) << " ";
                    positionsSTL.push_back(stdHashMap.at(tokenToID.findByCopy(word)).at(m).positions.at(p));
                }
                std::cout << "]" << std::endl;
            }
            iterations++;
        }
        // else {
        //     deletedSTL++;
        // }
        std::cout << "Result in my hash table: " << std::endl;
        std::vector<Posting>* copy = myHashMap.get(tokenToID.findByCopy(word));
        if (copy != nullptr) {
            for (std::size_t k = 0; k < copy->size(); k++) {
                std::cout << "docID " << copy->at(k).documentID << " ";
                std::cout << "[ ";
                for (int n = 0; n < copy->at(k).positions.size(); n++) {
                    std::cout << copy->at(k).positions.at(n) << " ";
                    positionsMyTable.push_back(copy->at(k).positions.at(n));
                }
                std::cout << "]" << std::endl;
            }
        }
        // else {
        //     deletedMyTable++;
        // }
        std::sort(positionsMyTable.begin(), positionsMyTable.end());
        std::sort(positionsSTL.begin(), positionsSTL.end());
        if (iterations > 10) break;
    }
    // if (correct == iterations && deletedMyTable == deletedSTL) {
    //     //std::cout << "All postings lists were found. Life is worth living" << std::endl;
    // }
}

int main() {
    testHashMap();
}