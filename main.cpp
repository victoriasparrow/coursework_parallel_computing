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
#include "ThreadPool.h"
#include "constants.h"

std::vector<std::pair<std::string, uint32_t>> getFilesNamesInDirectory(const std::filesystem::path& path, SearchEngine& data) {
    std::vector<std::pair<std::string, uint32_t>> newFiles;
    static uint32_t nextDocID = 1; // for sheduler thread
    if (!std::filesystem::exists(path)) {
        return newFiles;
    }
    for (const auto& entry : std::filesystem::directory_iterator(path)) {
        if (entry.path().extension().string() == ".txt") {
            std::string file = entry.path().string();
            if (data.docToID.find(file) != 0) {
                continue;
            }
            uint32_t id = nextDocID;
            nextDocID++;
            data.docToID.insert(file, id);
            data.idToDoc.insert(id, file);
            newFiles.emplace_back(file, id);
            //std::cout << "Found new file: " << file << ", id: " << id << std::endl;
        }
    }
    return newFiles;
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
    constexpr std::string_view delims = " \t\n\r\v\f1234567890_/!.,@#$%^&*();:?{}<>|`~[]+=\"\\-'";
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

void processDocument(const std::string& fileName, std::unordered_map<std::string, std::vector<uint32_t>>& pos, const std::unordered_set<std::string, stringHash, std::equal_to<>>& stopWordsMap) {
    static std::size_t allUniqueWords = 0;
    std::ifstream file;
    std::string line;
    file.open(fileName);
    if (file.is_open()) {
        uint32_t wordCountDoc = 1;
        while (std::getline(file, line )) {
            for (char &toLower : line) {
                if( toLower >= 'A' && toLower <= 'Z' ) {
                    toLower += 'a' - 'A';
                }
            }
            processLine(line, pos, stopWordsMap, wordCountDoc);
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

int main() {
    SearchEngine data;
    std::unordered_set<std::string, stringHash, std::equal_to<>> stopWordsMap;
    loadStopWords(stopWordsMap);
    constexpr int threadNumber = constants::threadsNumber;
    auto newFiles = getFilesNamesInDirectory(constants::corpusPath, data);

    threadPool pool;
    pool.initialize(threadNumber);

    if (newFiles.empty()) {
        std::cerr << "No files found. No need for ThreadPool." << std::endl;
        pool.terminate();
        return 0;
    }
    std::atomic<int> filesRemaining = newFiles.size();
    std::mutex m;
    std::condition_variable cv;

    auto indexStart = std::chrono::steady_clock::now();

    for (const auto& pair: newFiles) {
        std::string filePath = pair.first;
        uint32_t docID = pair.second;
        pool.add_task([filePath, docID, &data, &stopWordsMap, &filesRemaining, &m, &cv]() {
            std::unordered_map<std::string, std::vector<uint32_t>> wordPositionsLocal;
            processDocument(filePath, wordPositionsLocal, stopWordsMap);
            for (auto& it : wordPositionsLocal) {
                const std::string& word = it.first;
                uint32_t tokenID = data.getTokenID(word);
                Posting thisDoc (docID, std::move(it.second));
                std::vector<Posting> postings;
                postings.push_back(thisDoc);
                data.InvertedIndex.insert(tokenID, std::move(postings));
            }
            if (filesRemaining.fetch_sub(1) == 1) {
                std::lock_guard<std::mutex> lock(m);
                cv.notify_one();
            }
        }, 1);
    }
    std::unique_lock<std::mutex> lock(m);
    cv.wait(lock, [&filesRemaining](){ return filesRemaining.load() == 0; });
    lock.unlock();
    auto indexEnd = std::chrono::steady_clock::now();
    auto indexTime = std::chrono::duration_cast<std::chrono::milliseconds>(indexEnd - indexStart).count();
    std::cout << "Number of threads: " << constants::threadsNumber << ". Building InvertedIndex took " << indexTime << " ms." << std::endl;
    std::cout << "[Inverted Index] Load factor: " << data.InvertedIndex.currentLoad() << std::endl;
    std::cout << "[Inverted Index] Bucket array size: " << data.InvertedIndex.bucket_count() << std::endl;
    std::cout << "[Inverted Index] Counter size: " << data.InvertedIndex.size() << std::endl;
    std::cout << "[DocToID] Load factor: " << data.docToID.currentLoad() << std::endl;
    std::cout << "[idToDoc] Load factor: " << data.idToDoc.currentLoad() << std::endl;
    std::cout << "[tokenToID] Load factor: " << data.tokenToID.currentLoad() << std::endl;
    std::cout << "Total unique words: " << data.nextToken.load();
    data.tokenToID.printBuckets(50);

    // std::vector<std::string> words = {"spectacular", "extravagant", "sensual", "archaic", "sherlock", "nostalgia", "university", "drop"};
    //  for (const std::string &word: words) {
    //      std::cout << "\n・ ✦ ・ The token is " << word << " ・ ✦ ・" << std::endl;
    //      uint32_t tokenID = data.tokenToID.find(word);
    //      std::cout << "tokenID is "<< tokenID << std::endl;
    //      std::vector<Posting> copy = data.InvertedIndex.find(tokenID);
    //      for (std::size_t k = 0; k < copy.size(); k++) {
    //          std::cout << "docID: " << copy.at(k).documentID << " doc name " << data.idToDoc.find(copy.at(k).documentID) << std::endl;
    //          std::cout << "[ ";
    //          for (int n = 0; n < copy.at(k).positions.size(); n++) {
    //              std::cout << copy.at(k).positions.at(n) << " ";
    //          }
    //          std::cout << "] size is " << copy.at(k).positions.size() << " " << std::endl;
    //      }
    //  }
    // //data.InvertedIndex.statistics();
    // std::cout << "wordIds number " << data.nextToken.load() - 1 << std::endl;
    //
    // std::this_thread::sleep_for(std::chrono::seconds(4));
    //
    // auto initialStart = std::chrono::steady_clock::now();
    // uint32_t tokenID = data.tokenToID.find("leopard");
    // std::vector<Posting> copy1 = data.InvertedIndex.findAndMove(tokenID);
    // auto initialEnd = std::chrono::steady_clock::now();
    // auto execDurationInitial = std::chrono::duration_cast<std::chrono::microseconds>(initialEnd - initialStart).count();
    //
    // std::this_thread::sleep_for(std::chrono::seconds(4));
    //
    // auto headStart = std::chrono::steady_clock::now();
    // uint32_t tokenID2 = data.tokenToID.find("leopard");
    // std::vector<Posting> copy2 = data.InvertedIndex.find(tokenID2);
    // auto headEnd = std::chrono::steady_clock::now();
    // auto execDurationHead = std::chrono::duration_cast<std::chrono::microseconds>(headEnd - headStart).count();
    // std::cout << "\nMove-at-front: " << execDurationInitial << std::endl;
    // std::cout << "Term is at head: " << execDurationHead << std::endl;
    // cpu messes up this horrible test but move-at-front done
    pool.terminate();
    return 0;
}