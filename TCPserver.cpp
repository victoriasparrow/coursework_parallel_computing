#include <thread>
#include <fstream>
#include <unordered_set>
#include "ConcurrentHashMap.h"
#include "SeachEngine.h"
#include "TCPnetworking.h"
#include "ThreadPool.h"

std::vector<std::pair<std::string, uint32_t>> getFilesNamesInDirectory(const std::filesystem::path& path, SearchEngine& data) {
    std::vector<std::pair<std::string, uint32_t>> newFiles;
    static uint32_t nextDocID = 1; // for scheduler thread
    if (!std::filesystem::exists(path)) {
        return newFiles;
    }
    for (const auto& entry : std::filesystem::directory_iterator(path)) {
        if (entry.path().extension().string() == ".txt") {
            std::string file = entry.path().filename().string();
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


std::string createLines(std::string& fileName, std::vector<uint32_t> positions) {
    constexpr int kContextWindow = 30;
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
                iterator++;
            }

            currentWordCount++;

            current = end;
            if (current == std::string::npos) break;
        }
    }
    return results;
}

void processLine(const std::string_view line, std::unordered_map<std::string, std::vector<uint32_t>>& pos, const std::unordered_set<std::string, stringHash, std::equal_to<>>& stopWordsMap, uint32_t& wordCount) {
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

void processDocument(const std::string& fileName, std::unordered_map<std::string, std::vector<uint32_t>>& pos, const std::unordered_set<std::string, stringHash, std::equal_to<>>& stopWordsMap) {
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

void searchAndSend(int socketFD, const std::string& query, int page, SearchEngine& data) {
    uint32_t tokenID = data.tokenToID.find(query);
    if (tokenID == 0) {
        tcp::sendQueryResult(socketFD, 0, {});
        return;
    }
    std::vector<QueryResult> results;
    std::vector<Posting> postings = data.InvertedIndex.find(tokenID);
    uint32_t totalDocs = postings.size();
    size_t startIndex = 0, stopIndex = 0;
    startIndex = page * kPageLength; // 0
    stopIndex = std::min(startIndex + kPageLength, postings.size());
    for (size_t i = startIndex; i < stopIndex; i++) {
        QueryResult res;
        res.docID = postings[i].documentID;
        uint32_t documentID = postings[i].documentID;
        std::string fileName = data.idToDoc.find(documentID);
        res.docName = data.idToDoc.find(documentID);
        res.termFrequency = postings[i].positions.size();
        res.lines = createLines(fileName, postings[i].positions);
        results.push_back(res);
    }
    tcp::sendQueryResult(socketFD, totalDocs, results);
}

void handleClient(int socketFD, SearchEngine& data) {
    tcp::socketReceiveTimeOut(socketFD, 5);
    char buffer[kMaxPayload];
    uint8_t tag = 0;
    uint32_t length = 0;
    if (tcp::receiveData(socketFD, buffer, &tag, &length) != 0) {
        std::cerr << "server: handleClient, handshake close" << std::endl;
        close(socketFD);
        return;
    }
    if (tag != (uint8_t)TagTLV::kHello) {
        std::cerr << "server: client is rude, no hello" << std::endl;
        tcp::sendData(socketFD, (uint8_t)TagTLV::kError, nullptr, 0);
        close(socketFD);
        return;
    }
    const char* helloBuddy = "Hello, my dove!";
    if (tcp::sendData(socketFD, (uint8_t)TagTLV::kServerHello, helloBuddy, strlen(helloBuddy)) != 0) {
        std::cerr << "server: couldn't send hello back" << std::endl;
        close(socketFD);
        return;
    }
    if (tcp::receiveData(socketFD, buffer, &tag, &length) == 0) {
        if (tag ==(uint8_t)TagTLV::kQuery) {
            // buffer [tag 1 byte] [length 4 bytes] | payload [page 4 bytes] [actual string n bytes]
            if (length < 4) {
                tcp::sendData(socketFD, (uint8_t)TagTLV::kError, nullptr, 0);
            }
            uint32_t networkPage = 0;
            memcpy(&networkPage, buffer, sizeof(uint32_t));
            uint32_t page = tcp::toBigEndian(networkPage);
            std::string term(buffer + sizeof(uint32_t), length - sizeof(uint32_t));
            for (char& c : term) {
                c = tolower(c);
            }
            searchAndSend(socketFD, term, page, data);
        }
        else {
            std::cerr << "server: what tag are you sending me??" << std::endl;
        }
    }
    else {
        std::cout << "server: client with socketFD " << socketFD << " disconnected or time out!" << std::endl;
    }
    close(socketFD);
    std::cout << "thread " << std::this_thread::get_id() << " let the client go" << std::endl;
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
    std::atomic<size_t> filesRemaining = newFiles.size();
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
            // if (filesRemaining.fetch_sub(1) == 1) {
            //     std::lock_guard<std::mutex> lock(m);
            //     cv.notify_one();
            // }
        }, 1);
    }
    //std::unique_lock<std::mutex> lock(m);
    //cv.wait(lock, [&filesRemaining](){ return filesRemaining.load() == 0; });
    //lock.unlock();
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

    int listeningFD = tcp::createSocket(nullptr, kPort, true);
    if (listeningFD == -1) {
        std::cerr << "server: failed to ignite" << std::endl;
        return -1;
    }
    while(true) {
        int newFD = tcp::acceptClients(listeningFD);
        if (newFD == -1) continue;
        pool.add_task([newFD, &data]() {
            handleClient(newFD, data);
        }, 2);
    }
    pool.terminate();
    close(listeningFD);
    return 0;
}