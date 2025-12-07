#include <fstream>
#include <thread>
#include <map>
#include <unordered_set>

#include "ConcurrentHashMap.h"
#include "HTTPserver.h"
#include "SeachEngine.h"
#include "TCPnetworking.h"
#include "ThreadPool.h"
#include "TextParser.h"

std::vector<std::pair<std::string, uint32_t>> getFilesNamesInDirectory(const std::filesystem::path& path, SearchEngine& data) {
    std::vector<std::pair<std::string, uint32_t>> newFiles;
    static std::atomic<uint32_t> nextDocID = 1; // for scheduler thread
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
            nextDocID.fetch_add(1);
            data.docToID.insert(file, id);
            data.idToDoc.insert(id, file);
            newFiles.emplace_back(file, id);
            //std::cout << "Found new file: " << file << ", id: " << id << std::endl;
        }
    }
    return newFiles;
}

void searchAndSendTCP(int socketFD, const std::string& query, int page, SearchEngine& data) {
    SearchResult result = getSearchResult(query, page, data);
    tcp::sendQueryResult(socketFD, result.totalDocs, result.results);
}

void handleTCPClient(int socketFD, SearchEngine& data) {
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
                return;
            }
            uint32_t networkPage = 0;
            memcpy(&networkPage, buffer, sizeof(uint32_t));
            uint32_t page = tcp::toBigEndian(networkPage);
            std::string term(buffer + sizeof(uint32_t), length - sizeof(uint32_t));
            for (char& c : term) {
                c = tolower(c);
            }
            searchAndSendTCP(socketFD, term, page, data);
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

void indexingTask(threadPool& pool, const std::string& filePath, uint32_t docID, SearchEngine& data, const std::unordered_set<std::string, txtparcer::stringHash, std::equal_to<>>& stopWordsMap, std::function<void()> stop) {
    pool.add_task([filePath, docID, &data, &stopWordsMap, stop]() {
        std::unordered_map<std::string, std::vector<uint32_t>> wordPositionsLocal;
        txtparcer::processDocument(filePath, wordPositionsLocal, stopWordsMap);
        for (auto& it : wordPositionsLocal) {
            const std::string& word = it.first;
            uint32_t tokenID = data.getTokenID(word);
            Posting thisDoc (docID, std::move(it.second));
            std::vector<Posting> postings;
            postings.push_back(thisDoc);
            data.InvertedIndex.insert(tokenID, std::move(postings));
        }
        if (stop) {
            stop();
        }
    }, 1);
}

int main() {
    SearchEngine data;
    std::unordered_set<std::string, txtparcer::stringHash, std::equal_to<>> stopWordsMap;
    txtparcer::loadStopWords(stopWordsMap);
    constexpr int threadNumber = constants::threadsNumber;
    std::vector<std::pair<std::string, uint32_t>> initialFiles = getFilesNamesInDirectory(constants::corpusPath, data);

    threadPool pool;
    pool.initialize(threadNumber);

    if (initialFiles.empty()) {
        std::cerr << "server: no files found, no need for ThreadPool." << std::endl;
        pool.terminate();
        return 0;
    }
    std::atomic<size_t> filesRemaining = initialFiles.size();
    std::mutex m;
    std::condition_variable cv;

    auto indexStart = std::chrono::steady_clock::now();

    for (const auto& pair: initialFiles) {
        std::string filePath = pair.first;
        uint32_t docID = pair.second;
        auto waitIndexing = [&filesRemaining, &m, &cv]() {
             if (filesRemaining.fetch_sub(1) == 1) {
                 std::lock_guard<std::mutex> lock(m);
                 cv.notify_one();
             }
        };
        indexingTask(pool, filePath, docID, data, stopWordsMap, waitIndexing);
    }
    std::unique_lock<std::mutex> lock(m);
    cv.wait(lock, [&filesRemaining](){ return filesRemaining.load() == 0; });
    lock.unlock();
     auto indexEnd = std::chrono::steady_clock::now();
     auto indexTime = std::chrono::duration_cast<std::chrono::milliseconds>(indexEnd - indexStart).count();
    std::thread schedulerThread([&data, &pool, &stopWordsMap]() {
        while (true) {
            std::this_thread::sleep_for(std::chrono::seconds(20));
            std::vector<std::pair<std::string, uint32_t>> newFiles = getFilesNamesInDirectory(constants::corpusPath, data);
            for (const auto& pair : newFiles) {
                std::string filePath = pair.first;
                uint32_t docID = pair.second;
                std::cout << "scheduler: found new file, name " << filePath << " and id " << docID << std::endl;
                indexingTask(pool, filePath, docID, data, stopWordsMap, nullptr);
            }
        }
    });
    schedulerThread.detach();

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
            char firstByte = 0;
            ssize_t byte = recv(newFD, &firstByte, 1, MSG_PEEK);
            if (byte <= 0) {
                close(newFD);
                return;
            }
            if (firstByte == (char)TagTLV::kHello) {
                handleTCPClient(newFD, data);
            }
            else handleHTTPClient(newFD, data);
        }, 2);
    }
    pool.terminate();
    close(listeningFD);
    return 0;
}