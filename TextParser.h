#ifndef TEXTPARSER_H
#define TEXTPARSER_H
#include <string_view>
#include <string>
#include <unordered_map>
#include <unordered_set>


namespace txtparcer {
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
    std::string createLines(std::string& fileName, std::vector<uint32_t> positions);
    void processLine(const std::string_view line, std::unordered_map<std::string, std::vector<uint32_t>>& pos, const std::unordered_set<std::string, stringHash, std::equal_to<>>& stopWordsMap, uint32_t& wordCount);
    void processDocument(const std::string& fileName, std::unordered_map<std::string, std::vector<uint32_t>>& pos, const std::unordered_set<std::string, stringHash, std::equal_to<>>& stopWordsMap);
    void loadStopWords(std::unordered_set<std::string, stringHash, std::equal_to<>>& stopWordsMap);
};


#endif //TEXTPARSER_H