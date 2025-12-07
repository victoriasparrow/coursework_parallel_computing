#ifndef CONCURRENTHASHMAP_H
#define CONCURRENTHASHMAP_H
#include <cstddef>
#include <iostream>
#include <functional>
#include <algorithm>
#include <shared_mutex>
#include "LinkedList.h"
#include "constants.h"
#define LOCKS 16
#include <iostream>
#include <mutex>

struct Posting{
    uint32_t documentID = 0;
    std::vector<uint32_t> positions; // in this particular document the positions of the word are 5,6,78 etc
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

struct paddedMutex {
    std::shared_mutex mutex;
    char padding[128 - sizeof(std::mutex)];
};

template <typename K, typename V>
class ConcurrentHashMap{
    double loadFactor{};
    std::size_t arraySize{};
    std::atomic<std::size_t> counter{};
    LinkedList<K, V>* bucketsArray;
    paddedMutex* mutexes;

public:
    ConcurrentHashMap(std::size_t size = 1000): loadFactor(0.8), arraySize(size) {
        bucketsArray = new LinkedList<K, V>[arraySize];
        mutexes = new paddedMutex[LOCKS];
        std::cout << "arraySize " << arraySize << " pointer is " << bucketsArray << std::endl;
    }

    ~ConcurrentHashMap() {
        delete[] bucketsArray;
        delete[] mutexes;
    };

    std::size_t hash(const K& key) const {
        return std::hash<K>{}(key);
    }

    std::size_t getIndex(const K& key) const {
        std::size_t index = hash(key) % arraySize;
        if (index >= arraySize) {
            std::cout << "HELLO" << std::endl;
        }
        return hash(key) % arraySize;
    }

    std::size_t getLockIndex(const K& key) const {
        return hash(key) % LOCKS;
    }

    double currentLoad() const {
        return static_cast<double>(counter) / static_cast<double>(arraySize);
    }

    std::size_t size() const {
        return counter;
    }

    std::size_t bucket_count() const {
        return arraySize;
    }

    bool insert(K key, V value);
    V find(const K& key) const;
    void moveToFront(const K& key) const;
    bool erase(const K& key);
    void relocateMemory(); // i am scared of you
    void statistics() const;
};

template<typename T> struct is_vector :std::false_type {};
template <typename... Args> struct is_vector < std::vector<Args...>> :std::true_type {};

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
void ConcurrentHashMap<K, V>::relocateMemory() {
    std::size_t oldSize = arraySize;
    std::size_t newSize = arraySize * 1.6;
    //std::cout << "the old size is " << oldSize << " and new size is " << newSize << std::endl;
    arraySize = newSize;
    LinkedList<K, V>* newBucketsArray = new LinkedList<K, V>[newSize];
    for (std::size_t i = 0; i < oldSize; ++i) {
        LinkedList<K, V>* innerArray = &bucketsArray[i];
        HashNode<K, V>* current = innerArray->get_head(); // okay we've got pointer head of inner linked list
        while (current != nullptr) { // now all nodes have to find a new home
            std::size_t newIndex = getIndex(current->key);
            LinkedList<K, V>* newInnerArray = &newBucketsArray[newIndex];
            newInnerArray->push_back(std::move(current->key), std::move(current->value));
            current = current->nextNode;
        }
    }
    delete[] bucketsArray;
    bucketsArray = newBucketsArray;
}

template<typename K, typename V>
bool ConcurrentHashMap<K, V>::erase(const K& key) {
    std::size_t index = getIndex(key);
    std::size_t idLock = getLockIndex(key);
    std::unique_lock<std::shared_mutex> lock(mutexes[idLock].mutex);
    LinkedList<K, V>* innerArray = &bucketsArray[index];
    HashNode<K, V>* found = innerArray->find(key);
    if (found == nullptr) {
        return false;
    }
    innerArray->remove(key);
    counter.fetch_sub(1);
    return true;
}

template<typename K, typename V>
V ConcurrentHashMap<K, V>::find(const K& key) const{
    V result = {};
    bool moveNeeded = false;
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
        moveToFront(key);
    }
    return result;
}

template<typename K, typename V>
void ConcurrentHashMap<K, V>::moveToFront(const K& key) const{
    std::size_t idLock = getLockIndex(key);
    std::unique_lock<std::shared_mutex> lock(mutexes[idLock].mutex);
    std::size_t index = getIndex(key);
    LinkedList<K, V>* innerList = &bucketsArray[index];
    HashNode<K, V>* neededNode = innerList->find(key);
    if (neededNode == nullptr) {
        return;
    }
    if (neededNode != innerList->get_head()) {
        innerList->moveToFront(key);
    }
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