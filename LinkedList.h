#ifndef LINKEDLIST_H
#define LINKEDLIST_H
#include <vector>

struct Posting{
    u_int32_t documentID = 0;
    std::vector<u_int32_t> positions; // in this particular document the positions of the word are 5,6,78 etc
    Posting();
    Posting(u_int32_t docID, std::vector<u_int32_t>&& pos): documentID(docID), positions(pos) {};
};

struct HashNode { // termID = {docID1, (position1, position2, ...},{docID2, (position1, position2, ...})
    u_int32_t termID;
    std::vector<Posting> postings; //postings[0] docID = 1 positions = {1, 56, 14} // todo: add a small axillary which postings will be written to and then merged into sorted main postings vector
    HashNode *nextNode = nullptr;
};

class LinkedList{
    int size;
    HashNode* head;
    HashNode* tail;
public:
    LinkedList();
    ~LinkedList();

    HashNode* returnHead() const;

    void remove(u_int32_t tokenID);

    int get_size() const;
    void print() const;
    void get(u_int32_t tokenID, std::vector<Posting>& emptyPostings) const;

    HashNode* find(u_int32_t tokenID) const;

    void push_back(u_int32_t tokenID, std::vector<Posting>&& postings);
    void push_front(u_int32_t tokenID, std::vector<Posting>&& postings);

    bool pop_back();
    bool pop_front();

    void clear();
};

#endif // LINKEDLIST_H