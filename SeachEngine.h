#ifndef SEARCHENGINE
#define SEARCHENGINE
#include "ConcurrentHashMap.h"

struct Posting{
    uint32_t documentID = 0;
    std::vector<uint32_t> positions;
    Posting();
    Posting
    (uint32_t docID, std::vector<uint32_t>&& pos): documentID(docID), positions(pos) {};

    bool operator<(const Posting& other) const {
        return documentID < other.documentID;
    }

    bool operator==(const Posting& other) const {
        return documentID == other.documentID && positions == other.positions;
    }
};

struct QueryResult {
    uint32_t docID;
    std::string docName;
    uint32_t termFrequency;
    std::string lines;
};

struct SearchResult {
    uint32_t totalDocs;
    std::vector<QueryResult> results;
};

struct ParsedQuery {
    uint32_t page;
    std::string term;
};

struct SearchEngine {
    std::atomic<uint32_t> nextToken{1};
    ConcurrentHashMap<std::string, uint32_t> tokenToID{1430027};
    ConcurrentHashMap<std::string, uint32_t> docToID{18000};
    ConcurrentHashMap<uint32_t,std::string> idToDoc{18000};
    ConcurrentHashMap<uint32_t,std::vector<Posting>> InvertedIndex{1430027};

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

std::vector<Posting> getMatches(const std::string& query, SearchEngine& data);
std::vector<Posting> intersectQueries(std::vector<std::string> multipleQueries, SearchEngine& data);
std::vector<Posting> intersectPostings(const std::vector<Posting>& first, const std::vector<Posting>& second);
SearchResult getSearchResult(const std::string& input, int page, SearchEngine& data);

#endif // SEARCHENGINE