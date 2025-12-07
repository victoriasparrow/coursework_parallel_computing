#ifndef SEARCHENGINE
#define SEARCHENGINE
#include "ConcurrentHashMap.h"

struct SearchEngine {
    std::atomic<uint32_t> nextToken{1};
    ConcurrentHashMap<std::string, uint32_t> tokenToID{100000};
    ConcurrentHashMap<std::string, uint32_t> docToID{5000};
    ConcurrentHashMap<uint32_t,std::string> idToDoc{5000};
    ConcurrentHashMap<uint32_t,std::vector<Posting>> InvertedIndex{100000};

    std::vector<std::string> fileNames;

    std::mutex noBurntIDs;
    uint32_t getTokenID(const std::string &word) {
        uint32_t id = tokenToID.find(word);
        if (id != 0) return id;
        {
            std::lock_guard<std::mutex> lock(noBurntIDs);
            id = tokenToID.find(word);
            if (id != 0) return id;
            uint32_t newID = nextToken.load();
            bool success = tokenToID.insert(word, newID);
            if (success) {
                nextToken.fetch_add(1);
                return newID;
            }
        }
        return tokenToID.find(word);
    }
};

#endif // SEARCHENGINE