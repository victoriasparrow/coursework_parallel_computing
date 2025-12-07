#ifndef HASHMAP_H
#define HASHMAP_H
#include <cstddef>
#include <iostream>
#include <functional>
#include <algorithm>
#include "LinkedList.h"

struct Posting{
    u_int32_t documentID = 0;
    std::vector<u_int32_t> positions; // in this particular document the positions of the word are 5,6,78 etc
    Posting();
    Posting
    (u_int32_t docID, std::vector<u_int32_t>&& pos): documentID(docID), positions(pos) {};

    bool operator<(const Posting& other) const {
        return documentID < other.documentID;
    }

    bool operator==(const Posting& other) const {
        return documentID == other.documentID && positions == other.positions;
    }
};

template <typename K, typename V>
class HashMap{
    double loadFactor{};
    std::size_t arraySize{};
    std::size_t counter{};
    LinkedList<K, V>* bucketsArray;

public:
    HashMap(): loadFactor(0.8), arraySize(100000) {
        bucketsArray = new LinkedList<K, V>[arraySize];
    }
    ~HashMap() {
        delete[] bucketsArray;
    };

    std::size_t hash(const K& key) const {
        return std::hash<K>{}(key) % arraySize;
    }

    std::size_t getIndex(const K& key) const {
        return hash(key) % arraySize;
    }

    std::size_t size() const {
        return counter;
    }

    std::size_t bucket_count() const {
        return arraySize;
    }

    V findByCopy(const K& key) const;
    HashNode<K, V>* findByKey(const K& key) const;
    V* get(const K& key);
    void copyVectorByKey(const K& key, V& value) const;

    bool insert(K key, V value);
    void relocateMemory();
    void erase(const K& key);

    void statistics() const;
    void printValueByKey(const K& key) const;
    std::vector<std::pair<K,V>> iteratorCopy();
};

template<typename K, typename V>
HashNode<K, V>* HashMap<K, V>::findByKey(const K& key) const {
    std::size_t index = getIndex(key);
    LinkedList<K, V>* innerList = &bucketsArray[index];
    return innerList->find(key); // returns pointer to a node or nullptr
}

template<typename K, typename V>
void HashMap<K, V>::copyVectorByKey(const K& key, V& value) const {
    std::size_t index = getIndex(key);
    //std::cout << "index is " << index;
    LinkedList<K, V>* innerList = &bucketsArray[index];
    //std::cout << "pointer to innerList is " << innerList << std::endl;
    HashNode<K, V>* needed = innerList->find(key);
    if (needed != nullptr) {
        std::copy(needed->value.begin(), needed->value.end(), std::back_inserter(value));
    }
}

template<typename T> struct is_vector :std::false_type {};
template <typename... Args> struct is_vector < std::vector<Args...>> :std::true_type {};

// add a node, if a node with this key exists, just push new elements to the vector
template<typename K, typename V>
bool HashMap<K, V>::insert(K key, V value) {
    double newLoad = static_cast<double>(counter) / static_cast<double>(arraySize);
    if (newLoad >= loadFactor) {
        //relocateMemory();
    }
    HashNode<K, V>* neededNode = findByKey(key);
    if (neededNode != nullptr) { // such key exists
        if constexpr (is_vector<V>::value) { // just pushing into the vector new values
            // we think that neededNode->value vector is already sorted
            neededNode->value.reserve(neededNode->value.size() + value.size());
            auto size = neededNode->value.size(); // original size
            std::sort(value.begin(), value.end());
            // all added to the final vector
            neededNode->value.insert(neededNode->value.end(), value.begin(), value.end()); // add to the end
            std::inplace_merge(begin(neededNode->value), begin(neededNode->value) + size, end(neededNode->value));
            return true;
        }
        else {
            std::cout << "such key already exists" << std::endl;
            return false;
        }
    }
    // no such key
    std::size_t index = getIndex(key);
    LinkedList<K, V>* innerList = &bucketsArray[index];
    if constexpr (is_vector<V>::value) {
        std::sort(value.begin(), value.end());
    }
    innerList->push_back(std::move(key), std::move(value));
    counter++;
    return true;
}

template<typename K, typename V>
void HashMap<K, V>::relocateMemory() {
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
void HashMap<K, V>::erase(const K& key) {
    std::size_t index = getIndex(key);
    LinkedList<K, V>* innerArray = &bucketsArray[index];
    HashNode<K, V>* found = findByKey(key);
    if (found == nullptr) {
        return;
    }
    innerArray->remove(key);
    counter--;
}

template<typename K, typename V>
void HashMap<K, V>::statistics() const {
    std::size_t empty = 0;
    std::cout << "statistics" << std::endl;
    std::cout << "bucketSize is " << bucket_count() << " actual size is " << size() << std::endl;
    for (std::size_t i = 0; i < bucket_count(); i++) {
        LinkedList<K, V> *innerArray = &bucketsArray[i];
        //std::cout << innerArray << std::endl;
        if (innerArray->get_head() == nullptr) {
            empty++;
        }
        else {
            std::cout << i << " inner list has " << innerArray->get_size() << " nodes" << std::endl;
        }
    }
    std::cout << " empty is " << empty << std::endl;
}

template<typename K, typename V>
void HashMap<K, V>::printValueByKey(const K& key) const {
    HashNode<K, V>* neededNode = findByKey(key);
    if (neededNode == nullptr) {
        //std::cout << " no such node" << std::endl;
        return;
    }

    if constexpr (is_vector<V>::value) {
        for (std::size_t i; i < neededNode->value.size(); i++) {
            std::cout << neededNode->value.at(i) << " ";
        }
    }
    else {
        //std::cout << " value is " << neededNode->value << std::endl;
    }
}

template<typename K, typename V>
V* HashMap<K, V>::get(const K& key) {
    HashNode<K, V>* neededNode = findByKey(key);
    if (neededNode != nullptr) {
        return &(neededNode->value);
    }
    return nullptr;
}

template<typename K, typename V>
V HashMap<K, V>::findByCopy(const K& key) const{
    HashNode<K, V>* neededNode = findByKey(key);
    if (neededNode != nullptr) {
        return neededNode->value;
    }
    return 0;
}

template<typename K, typename V>
std::vector<std::pair<K,V>> HashMap<K, V>::iteratorCopy() {
    std::vector<std::pair<K,V>> copy;
    for (std::size_t i = 0; i < bucket_count(); ++i) {
        LinkedList<K, V>* innerArray = &bucketsArray[i];
        HashNode<K, V>* current = innerArray->get_head();
        while (current != nullptr) {
            copy.push_back({current->key, current->value});
            current = current->nextNode;
        }
    }
    return copy;
}

#endif //HASHMAP_H