#include "SeachEngine.h"
#include "TCPnetworking.h"
#include "TextParser.h"

std::vector<Posting> intersectPostings(const std::vector<Posting>& first, const std::vector<Posting>& second) {
    std::vector<Posting> answer;
    auto firstIterator = first.begin();
    auto secondIterator = second.begin();
    while (firstIterator != first.end() && secondIterator != second.end()) {
        if (firstIterator->documentID == secondIterator->documentID) {
            answer.push_back(*firstIterator);
            ++firstIterator;
            ++secondIterator;
        }
        else if (firstIterator->documentID < secondIterator->documentID){
            ++firstIterator;
        }
        else {
            ++secondIterator;
        }
    }
    return answer;
}

std::vector<Posting> intersectQueries(std::vector<std::string> multipleQueries, SearchEngine& data) {
    if (multipleQueries.empty()) return {};
    //sorting by increasing frequency
    std::sort(multipleQueries.begin(), multipleQueries.end(),[&data](const std::string& a, const std::string& b) {
        size_t sizeA = 0, sizeB = 0;
        uint32_t tokenA = data.tokenToID.find(a);
        uint32_t tokenB = data.tokenToID.find(b);
        if (tokenA == 0) sizeA = 0; else sizeA = data.InvertedIndex.find(tokenA).size();
        if (tokenB == 0) sizeB = 0; else sizeB = data.InvertedIndex.find(tokenB).size();
        return sizeA < sizeB;
    });
    for (const auto& word : multipleQueries) {
        std::cout << word << std::endl;
    }
    uint32_t smallest = data.tokenToID.find(multipleQueries.at(0)); // if rarest term doesn't exist, exiting
    if (smallest == 0) return {};
    auto results = data.InvertedIndex.find(smallest);
    if (results.empty()) return {};
    for (int i = 1; i < multipleQueries.size(); ++i) {
        if (results.empty()) {
            break;
        }
        uint32_t tokenID = data.tokenToID.find(multipleQueries.at(i));
        if (tokenID == 0) return {};
        auto postings = data.InvertedIndex.find(tokenID);
        if (postings.empty()) return  {};
        results = intersectPostings(results, postings);
    }
    return results;
}

std::vector<Posting> getMatches(const std::string& query, SearchEngine& data) {
    auto queryVector = txtparcer::booleanQuery(query);
    if (queryVector.empty()) {
        return {};
    }
    else if (queryVector.size() == 1) {
        uint32_t tokenID = data.tokenToID.find(queryVector.at(0));
        if (tokenID == 0) return {};
        return data.InvertedIndex.find(tokenID);
    }
    else {
        return intersectQueries(queryVector, data);
    }
}

SearchResult getSearchResult(const std::string& input, int page, SearchEngine& data) {
    std::vector<Posting> matches = getMatches(input, data);
    uint32_t totalDocs = matches.size();
    size_t startIndex = 0, stopIndex = 0;
    startIndex = page * kPageLength; // 0
    if (startIndex >= totalDocs) {
        return {totalDocs, {}};
    }
    stopIndex = std::min(startIndex + kPageLength, (size_t)totalDocs);
    std::vector<QueryResult> results;
    results.reserve(stopIndex - startIndex);
    for (size_t i = startIndex; i < stopIndex; i++) {
        QueryResult res;
        res.docID = matches.at(i).documentID;
        std::string fileName = data.idToDoc.find(res.docID);
        res.docName = fileName;
        res.termFrequency = matches.at(i).positions.size(); // frequency of the rarest word
        res.lines = txtparcer::createLines(fileName, matches[i].positions);
        results.push_back(res);
    }
    return {totalDocs, results};
}