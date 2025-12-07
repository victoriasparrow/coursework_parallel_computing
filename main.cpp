#include <iostream>
#include <string_view>
#include <fstream>
#include <unordered_set>
#include <cstddef>
#include <thread>
#include <utility>
#include <unistd.h>
#include "ConcurrentHashMap.h"
#include "SeachEngine.h"
#include "constants.h"

std::mutex print;

u_int32_t getFilesNamesInDirectory(const std::filesystem::path& path, ConcurrentHashMap<std::string, u_int32_t>& docToID, std::vector<std::string>& fileNames) {
    u_int32_t nextDocID = 1;
    if (!std::filesystem::exists(path)) {
        return 0;
    }
    for (const auto& entry : std::filesystem::directory_iterator(path)) {
        if (entry.path().extension().string() == ".txt") {
            std::cout << entry.path().string() << std::endl;
            fileNames.push_back(entry.path().string());
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

void processLine(const std::string_view line, std::unordered_map<std::string, std::vector<u_int32_t>>& pos, const std::unordered_set<std::string, stringHash, std::equal_to<>>& stopWordsMap, u_int32_t& wordCount) {
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
        // {
        //     std::lock_guard<std::mutex> lock(print);
        //     std::cout << "thread id " << std::this_thread::get_id() << " is working" << std::endl;
        // }
        //std::cout << "tokenTemp " << tokenTemp << std::endl;
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

void processDocument(std::string_view fileName, std::unordered_map<std::string, std::vector<u_int32_t>>& pos, const std::unordered_set<std::string, stringHash, std::equal_to<>>& stopWordsMap) {
    static std::size_t allUniqueWords = 0;
    std::ifstream file;
    std::string line;
    file.open(fileName);
    if (file.is_open()) {
        u_int32_t wordCountDoc = 1;
        while (std::getline(file, line )) {
            std::transform(line.begin(), line.end(), line.begin(), ::tolower);
            processLine(line, pos, stopWordsMap, wordCountDoc);
        }
    }
    file.close();
    allUniqueWords += pos.size();
    std::cout << "allUniqueWords " << allUniqueWords << std::endl;
    std::cout << "unique words in local hash map: " << pos.size() << std::endl;
}

void worker(SearchEngine& data, std::atomic<int>& fileCounter, const std::unordered_set<std::string, stringHash, std::equal_to<>>& stopWordsMap) {
    u_int32_t fileNumber = data.fileNames.size();
    while (true) {
        int fileIndex = fileCounter.fetch_add(1);
        if (fileIndex >= fileNumber) break;

        const std::string& file = data.fileNames[fileIndex];
        u_int32_t docID = data.docToID.find(file);
        std::unordered_map<std::string, std::vector<u_int32_t>> wordPositionsLocal;
        processDocument(file, wordPositionsLocal, stopWordsMap);
        for (auto& it : wordPositionsLocal){
            const std::string& word = it.first;
            u_int32_t tokenID = data.getTokenID(word);
            Posting thisDoc (docID, std::move(it.second));
            std::vector<Posting> postings;
            postings.push_back(thisDoc);
            data.InvertedIndex.insert(tokenID, std::move(postings));
        }
    }
}

int main() {
    SearchEngine data;
    std::unordered_set<std::string, stringHash, std::equal_to<>> stopWordsMap;
    loadStopWords(stopWordsMap);
    getFilesNamesInDirectory(constants::corpusPath, data.docToID, data.fileNames);
    const int threadNumber = constants::threadsNumber;
    std::thread threads[threadNumber];
    std::atomic<int> fileCount{0};
    for (int i = 0; i < threadNumber; i++) {
        threads[i] = std::thread(worker, std::ref(data), std::ref(fileCount), std::ref(stopWordsMap));
    }
    for (int i = 0; i < threadNumber; i++) {
       if (threads[i].joinable()) {
           threads[i].join();
       }
    }

    std::vector<std::string> words = {"spectacular", "extravagant", "sensual", "archaic", "sherlock", "nostalgia", "university", "drop"};
     for (const std::string &pair: words) {
         std::string word = pair;
         std::cout << "\n・ ✦ ・ The token is " << pair << " ・ ✦ ・" << std::endl;
         u_int32_t tokenID = data.tokenToID.find(pair);
         std::cout << "tokenID is "<< tokenID << std::endl;
         std::vector<Posting> copy = data.InvertedIndex.find(tokenID);
         for (std::size_t k = 0; k < copy.size(); k++) {
             std::cout << "docID " << copy.at(k).documentID << " " << data.fileNames.at(copy.at(k).documentID - 1) << " ";
             std::cout << "[ ";
             for (int n = 0; n < copy.at(k).positions.size(); n++) {
                 std::cout << copy.at(k).positions.at(n) << " ";
             }
             std::cout << "] size is " << copy.at(k).positions.size() << " " << std::endl;
         }
     }

    data.InvertedIndex.statistics();
    std::cout << "wordIds number " << data.nextToken.load() - 1 << std::endl;

    std::this_thread::sleep_for(std::chrono::seconds(4));

    auto initialStart = std::chrono::steady_clock::now();
    u_int32_t tokenID = data.tokenToID.find("leopard");
    std::vector<Posting> copy1 = data.InvertedIndex.find(tokenID);
    auto initialEnd = std::chrono::steady_clock::now();
    auto execDurationInitial = std::chrono::duration_cast<std::chrono::microseconds>(initialEnd - initialStart).count();

    std::this_thread::sleep_for(std::chrono::seconds(4));

    auto headStart = std::chrono::steady_clock::now();
    u_int32_t tokenID2 = data.tokenToID.find("leopard");
    std::vector<Posting> copy2 = data.InvertedIndex.find(tokenID2);
    auto headEnd = std::chrono::steady_clock::now();
    auto execDurationHead = std::chrono::duration_cast<std::chrono::microseconds>(headEnd - headStart).count();
    std::cout << "\nBefore move-at-front: " << execDurationInitial << std::endl;
    std::cout << "After move-at-front: " << execDurationHead << std::endl;
    // cpu messes up this horrible test but move-at-front done
    return 0;
}