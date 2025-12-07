#ifndef CONCURRENTHASHMAP_H
#define CONCURRENTHASHMAP_H
#include <cstddef>
#include <new>
#include <iostream>
#include <functional>
#include <algorithm>
#include <shared_mutex>
#include "LinkedList.h"
#include "constants.h"
#include <iostream>
#include <mutex>

std::mutex printMutex;
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

#ifdef __APPLE__
    constexpr std::size_t CACHE_LINE = 128;
#else
    constexpr std::size_t CACHE_LINE = std::hardware_destructive_interference_size;
#endif

struct alignas(CACHE_LINE) paddedMutex { // 128 * 2 = 256
    std::shared_mutex mutex; // 168 is crazy my people
};

template <typename K, typename V>
class ConcurrentHashMap{
    double loadFactor{};
    std::size_t arraySize{};
    std::atomic<std::size_t> counter{};
    LinkedList<K, V>* bucketsArray;
    paddedMutex* mutexes;
    static constexpr int LOCKS = constants::locks;

public:
    explicit ConcurrentHashMap(std::size_t size): loadFactor(0.8), arraySize(size) {
        bucketsArray = new LinkedList<K, V>[arraySize];
        mutexes = new paddedMutex[LOCKS];
    }

    ~ConcurrentHashMap() {
        delete[] bucketsArray;
        delete[] mutexes;
    };

    std::size_t hash(const K& key) const {
        return std::hash<K>{}(key);
    }

    std::size_t getIndex(const K& key) const {
        return hash(key) % arraySize;
    }

    std::size_t getLockIndex(const K& key) const {
        std::size_t bucketIndex = getIndex(key);
        return (bucketIndex * LOCKS) / arraySize;
    }

    double currentLoad() const {
        return static_cast<double>(counter.load()) / static_cast<double>(arraySize);
    }

    std::size_t size() const {
        return counter.load();
    }

    std::size_t bucket_count() const {
        return arraySize;
    }

    bool insert(K key, V value);
    V find(const K& key) const;
    V findAndMove(const K& key) const;
    void moveToFront(const K& key) const;
    bool erase(const K& key);
    void statistics() const;
};

// add a node, if a node with this key exists, just push new elements to the vector
template<typename K, typename V>
bool ConcurrentHashMap<K, V>::insert(K key, V value) {
    double newLoad = currentLoad();
    if (newLoad >= loadFactor) {
        //relocateMemory();
    }
    std::size_t idLock = getLockIndex(key);
    std::unique_lock<std::shared_mutex> lock(mutexes[idLock].mutex);
    std::size_t index = getIndex(key);
    LinkedList<K, V>* innerList = &bucketsArray[index];
    HashNode<K, V>* neededNode = innerList->find(key);
    if (neededNode != nullptr) { // such key exists
        if constexpr (is_vector<V>::value) { // just pushing into the vector new values
            neededNode->value.reserve(neededNode->value.size() + value.size());
            auto originalSize = neededNode->value.size();
            std::sort(value.begin(), value.end());
            neededNode->value.insert(neededNode->value.end(), value.begin(), value.end()); // add to the end
            std::inplace_merge(begin(neededNode->value), begin(neededNode->value) + originalSize, end(neededNode->value));
            return true;
        }
        else {
            return false;
        }
    }
    // no such key
    if constexpr (is_vector<V>::value) {
        std::sort(value.begin(), value.end());
    }
    innerList->push_back(std::move(key), std::move(value));
    counter.fetch_add(1);
    return true;
}

template<typename K, typename V>
bool ConcurrentHashMap<K, V>::erase(const K& key) {
    std::size_t index = getIndex(key);
    std::size_t idLock = getLockIndex(key);
    std::unique_lock<std::shared_mutex> lock(mutexes[idLock].mutex);
    bool removed = bucketsArray[index].remove(key);
    if (removed) counter.fetch_sub(1);
    return removed;
}

template<typename K, typename V>
V ConcurrentHashMap<K, V>::find(const K& key) const{
    std::size_t idLock = getLockIndex(key);
    std::shared_lock<std::shared_mutex> lock(mutexes[idLock].mutex);
    std::size_t index = getIndex(key);
    LinkedList<K, V>* innerArray = &bucketsArray[index];
    HashNode<K, V>* neededNode = innerArray->find(key);
    if (neededNode != nullptr) {
        return neededNode->value;
    }
    return V{};
}

template<typename K, typename V>
V ConcurrentHashMap<K, V>::findAndMove(const K& key) const{
    V result = {};
    bool moveNeeded = false;
    K keyCopy = key;
    {
        std::size_t idLock = getLockIndex(key);
        std::shared_lock<std::shared_mutex> lock(mutexes[idLock].mutex);
        std::size_t index = getIndex(key);
        LinkedList<K, V>* innerArray = &bucketsArray[index];
        HashNode<K, V>* neededNode = innerArray->find(key);
        if (neededNode != nullptr) {
            result = neededNode->value;

            if (neededNode != innerArray->get_head()) {
                moveNeeded = true;
            }
        }
    } // unlocked
    if (moveNeeded) {
        moveToFront(keyCopy);
    }
    return result;
}

template<typename K, typename V>
void ConcurrentHashMap<K, V>::moveToFront(const K& key) const{
    std::size_t idLock = getLockIndex(key);
    std::unique_lock<std::shared_mutex> lock(mutexes[idLock].mutex);
    std::size_t index = getIndex(key);
    LinkedList<K, V>* innerList = &bucketsArray[index];
    innerList->moveToFront(key); // check for existence happens there
}

template<typename K, typename V>
void ConcurrentHashMap<K, V>::statistics() const {
    std::shared_lock<std::shared_mutex> locks[LOCKS];
    for (int i = 0; i < LOCKS; ++i) {
        locks[i] = std::shared_lock<std::shared_mutex>(mutexes[i].mutex);
    }
    std::size_t empty = 0;
    std::cout << "statistics" << std::endl;
    std::cout << "bucketSize is " << bucket_count() << " actual size is " << size() << std::endl;
    for (std::size_t i = 0; i < bucket_count(); i++) {
        LinkedList<K, V> *innerArray = &bucketsArray[i];
        if (innerArray->get_head() == nullptr) {
            empty++;
        }
        else {
            std::cout << i << " inner list has " << innerArray->get_size() << " nodes" << std::endl;
            //innerArray->print();
        }
    }
    std::cout << "number of empty buckets is " << empty << std::endl;
}

#endif // CONCURRENTHASHMAP_H