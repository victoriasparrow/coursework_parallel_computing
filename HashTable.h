#ifndef HASHTABLE_H
#define HASHTABLE_H
#include "LinkedList.h"

class HashTable{
    double loadFactor;
    int arraySize;
    int counter;
    LinkedList* bucketsArray;

public:
    HashTable();
    ~HashTable();

    std::size_t hash(u_int32_t tokenID) const;
    std::size_t getIndex(u_int32_t tokenID) const;
    int size() const;
    int bucket_size() const;

    HashNode* findByKey(u_int32_t tokenID) const;
    void copyByKey(u_int32_t tokenID, std::vector<Posting>& pos) const;

    bool insert(u_int32_t tokenID, std::vector<Posting>&& postings);
    void relocateMemory();
    void erase(u_int32_t tokenID);

    void statistics() const;
};

#endif // HASHTABLE_H