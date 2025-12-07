#include "HashTable.h"
#include <vector>
#include <iostream>
#include <cstddef>

HashTable::HashTable(): loadFactor(0.8), arraySize(1000), counter(0){
    bucketsArray = new LinkedList[arraySize];
}

HashTable::~HashTable() {
    delete[] bucketsArray;
}

int HashTable::bucket_size() const {
    return arraySize;
}

int HashTable::size() const {
    return counter;
}

std::size_t HashTable::hash(const u_int32_t tokenID) const {
    return std::hash<u_int32_t>{}(tokenID) % arraySize;
}

// std::size_t HashTable::hash(const u_int32_t tokenID) const{
//     return (17 * tokenID + 11);
// }

std::size_t HashTable::getIndex(u_int32_t tokenID) const {
    return hash(tokenID) % arraySize;
}

// std::size_t HashTable::hash(const u_int32_t tokenID) const{ // floor(m * frac(k * c)) // frac(x) = x - floor(x)
//     double c = (sqrt(5) / 2) - 1; //0.11803
//     double frac = tokenID * c - floor(tokenID * c);
//     int hash = floor(arraySize * frac);
//     hash = hash % arraySize;
//     return hash;
// }

HashNode* HashTable::findByKey(const u_int32_t tokenID) const {
    std::size_t index = getIndex(tokenID);
    LinkedList* innerList = &bucketsArray[index];
    return innerList->find(tokenID); // returns pointer to a node or nullptr
}

void HashTable::copyByKey(const u_int32_t tokenID, std::vector<Posting>& pos) const {
    std::size_t index = getIndex(tokenID);
    std::cout << "index is " << index;
    LinkedList* innerList = &bucketsArray[index];
    std::cout << "pointer to innerList is " << innerList << std::endl;
    HashNode* needed = innerList->find(tokenID);
    if (needed != nullptr) {
        std::copy(needed->postings.begin(), needed->postings.end(), std::back_inserter(pos));
    }
}

// add a node, if a node with this key exists, just push new elements to the vector
bool HashTable::insert(u_int32_t tokenID, std::vector<Posting>&& pos) {
    double newLoad = static_cast<double>(counter) / static_cast<double>(arraySize);
    if (newLoad >= loadFactor) {
        relocateMemory();
    }
    HashNode* neededNode = findByKey(tokenID);
    if (neededNode != nullptr) { // such term exists
        neededNode->postings.insert(std::end(neededNode->postings), std::begin(pos), std::end(pos));
        return true;
    }
    // no such term
    std::size_t index = getIndex(tokenID);
    LinkedList* innerList = &bucketsArray[index];
    innerList->push_back(tokenID, std::move(pos));
    counter++;
    return false;
}

void HashTable::relocateMemory() {
    int oldSize = arraySize;
    int newSize = arraySize * 1.6;
    //std::cout << "the old size is " << oldSize << " and new size is " << newSize << std::endl;
    arraySize = newSize;
    LinkedList* newBucketsArray = new LinkedList[newSize];
    for (int i = 0; i < oldSize; ++i) {
        LinkedList* innerArray = &bucketsArray[i];
        HashNode* current = innerArray->returnHead(); // okay we've got pointer head of inner linked list
        while (current != nullptr) { // now all nodes have to find a new home
            std::size_t newIndex = getIndex(current->termID);
            LinkedList* newInnerArray = &newBucketsArray[newIndex];
            newInnerArray->push_back(current->termID, std::move(current->postings));
            current = current->nextNode;
        }
        //innerArray->clear();
    }
    delete[] bucketsArray;
    bucketsArray = newBucketsArray;

}

void HashTable::erase(u_int32_t tokenID) {
    std::size_t index = getIndex(tokenID);
    LinkedList* innerArray = &bucketsArray[index];
    HashNode* found = findByKey(tokenID);
    if (found == nullptr) {
        return;
    }
    innerArray->remove(tokenID);
    counter--;
}

void HashTable::statistics() const {
    std::size_t empty = 0;
    std::cout << "statistics" << std::endl;
    std::cout << "bucketSize is " << bucket_size() << " actual size is " << size() << std::endl;
    for (std::size_t i = 0; i < bucket_size(); i++) {
        LinkedList *innerArray = &bucketsArray[i];
        std::cout << innerArray << std::endl;
        if (innerArray->returnHead() == nullptr) {
            empty++;
        }
        else {
            std::cout << i << " inner list has " << innerArray->get_size() << " nodes" << std::endl;
        }
    }
    std::cout << " empty is " << empty << std::endl;
}
