#ifndef SEARCHENGINE
#define SEARCHENGINE
#include "ConcurrentHashMap.h"

struct SearchEngine {
    std::atomic<uint32_t> nextToken{1};
    ConcurrentHashMap<std::string, uint32_t> tokenToID;
    ConcurrentHashMap<std::string, uint32_t> docToID;
    ConcurrentHashMap<uint32_t,std::vector<Posting>> InvertedIndex;

    std::vector<std::string> fileNames;

    uint32_t getTokenID(const std::string &word) {
        uint32_t id = tokenToID.find(word);
        if (id != 0) return id;

        uint32_t newID = nextToken.fetch_add(1);
        bool success = tokenToID.insert(word, newID); // false if such key exists
        if (success) {
            return newID;
        }
        return tokenToID.find(word);
    }
};

#endif // SEARCHENGINE