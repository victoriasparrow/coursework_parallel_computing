#include <fstream>
#include <thread>
#include <unordered_set>

#include "ConcurrentHashMap.h"
#include "SeachEngine.h"
#include "TCPnetworking.h"
#include "ThreadPool.h"
#include "TextParser.h"

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
        res.lines = txtparcer::createLines(fileName, postings[i].positions);
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
    std::unordered_set<std::string, txtparcer::stringHash, std::equal_to<>> stopWordsMap;
    txtparcer::loadStopWords(stopWordsMap);
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
            txtparcer::processDocument(filePath, wordPositionsLocal, stopWordsMap);
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
    // auto indexEnd = std::chrono::steady_clock::now();
    // auto indexTime = std::chrono::duration_cast<std::chrono::milliseconds>(indexEnd - indexStart).count();
    // std::cout << "Number of threads: " << constants::threadsNumber << ". Building InvertedIndex took " << indexTime << " ms." << std::endl;
    // std::cout << "[Inverted Index] Load factor: " << data.InvertedIndex.currentLoad() << std::endl;
    // std::cout << "[Inverted Index] Bucket array size: " << data.InvertedIndex.bucket_count() << std::endl;
    // std::cout << "[Inverted Index] Counter size: " << data.InvertedIndex.size() << std::endl;
    // std::cout << "[DocToID] Load factor: " << data.docToID.currentLoad() << std::endl;
    // std::cout << "[idToDoc] Load factor: " << data.idToDoc.currentLoad() << std::endl;
    // std::cout << "[tokenToID] Load factor: " << data.tokenToID.currentLoad() << std::endl;
    // std::cout << "Total unique words: " << data.nextToken.load();
    // data.tokenToID.printBuckets(50);

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