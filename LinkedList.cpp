#include "LinkedList.h"
#include <iostream>

LinkedList::LinkedList(): size(0), head(nullptr), tail(nullptr) {};
LinkedList::~LinkedList(){ clear(); };

void LinkedList::clear(){
    if (head == nullptr) {
        return;
    }
    HashNode* current = head;
    HashNode* next = nullptr;
    while (current != nullptr) {
        next = current->nextNode;
        delete current;
        current = next;
    }
    head = nullptr;
    tail = nullptr;
    size = 0;
}

void LinkedList::print() const{
    if (head == nullptr) {
        return;
    }
    HashNode* temp = head;
    while (temp != nullptr) {
        std::cout << "termID " << temp->termID;
        for (int i = 0; i < temp->postings.size(); i++) {
            std::cout << ", docID " << temp->postings.at(i).documentID << ", pos are ";
            for (int j = 0; j < temp->postings.at(i).positions.size(); j++) {
                std::cout << temp->postings.at(i).positions.at(j) << " ";
            }
        }
        std::cout << std::endl;
        temp = temp->nextNode;
    }
}

void LinkedList::push_back(const u_int32_t tokenID, std::vector<Posting>&& postings){
    HashNode* newNode = new HashNode();
    newNode->termID = tokenID;
    newNode->postings = std::move(postings);
    newNode->nextNode = nullptr;
    if (head == nullptr) {
        head = newNode;
        tail = newNode;
    }
    else {
        tail->nextNode = newNode;
        tail = newNode;
    }
    size++;
}

void LinkedList::push_front(const u_int32_t tokenID, std::vector<Posting>&& postings){
    if (head == nullptr) {
        push_back(tokenID, std::move(postings));
        return;
    }
    HashNode* newNode = new HashNode();
    newNode->termID = tokenID;
    newNode->postings = std::move(postings);
    newNode->nextNode = head;
    head = newNode;
    size++;
}

int LinkedList::get_size() const {
    return size;
}

bool LinkedList::pop_back() {
    if (head == nullptr) {
        return false;
    }
    if (head == tail) {
        delete head;
        head = nullptr;
        tail = nullptr;
        size = 0;
        return true;
    }
    HashNode* temp = head;
    while (temp->nextNode->nextNode != nullptr) {
        temp = temp->nextNode;
    }
    temp->nextNode = nullptr;
    delete tail;
    tail = temp;
    size--;
    return true;
}

bool LinkedList::pop_front() {
    if (head == nullptr) {
        return false;
    }
    if (head == tail) {
        delete head;
        head = nullptr;
        tail = nullptr;
        size = 0;
        return true;
    }
    HashNode* temp = head;
    head = temp->nextNode;
    delete temp;
    size--;
    return true;
}

// simple deep copy
void LinkedList::get(const u_int32_t tokenID, std::vector<Posting>& emptyPostings) const {
    if (head == nullptr) {
        return;
    }
    HashNode* temp = head;
    while (temp != nullptr) {
        if (temp->termID == tokenID) {
            std::copy(temp->postings.begin(), temp->postings.end(), std::back_inserter(emptyPostings));
        }
        temp = temp->nextNode;
    }
}

HashNode *LinkedList::find(u_int32_t tokenID) const {
    if (head == nullptr) {
        return nullptr;
    }
    HashNode* temp = head;
    while (temp != nullptr) {
        if (temp->termID == tokenID) {
            return temp;
        }
        temp = temp->nextNode;
    }
    return nullptr;
}

HashNode *LinkedList::returnHead() const {
    return head;
}

void LinkedList::remove(u_int32_t tokenID){
    if(!head){
        tail = nullptr;
        return;
    }
    if(head->termID == tokenID){
        HashNode* temp = head->nextNode;
        delete head;
        head = temp;
        return;
    }
    HashNode* current = head;
    while(current != nullptr){
        if(current->nextNode->termID == tokenID){
            HashNode* temp = current->nextNode->nextNode;
            delete current->nextNode;
            current->nextNode = temp;
            if(current->nextNode == nullptr){
                tail = current;
            }
            return;
        }
        current = current->nextNode;
    }
}