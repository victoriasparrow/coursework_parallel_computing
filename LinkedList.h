#ifndef LINKEDLIST_H
#define LINKEDLIST_H
#include <utility>
#include <iostream>

template <typename K, typename V>
struct HashNode {
    K key;
    V value;
    HashNode *nextNode = nullptr;
};

template <typename K, typename V>
class LinkedList{
    int size;
    HashNode<K,V>* head;
    HashNode<K,V>* tail;

public:
    LinkedList(): size(0), head(nullptr), tail(nullptr) {};
    ~LinkedList(){ clear(); };

    int get_size() const { return size; }

    HashNode<K,V>* get_head() const { return head; }

    bool remove(const K& key);
    void print() const;

    V* get(const K& key);
    HashNode<K, V>* findNode(const K &key) const;

    HashNode<K, V>* find(const K& key);

    void push_back(K key, V value);
    void push_front(K key, V value);
    bool pop_back();
    bool pop_front();
    void moveToFront(const K& key);
    void clear();
};

template <typename K, typename V>
void LinkedList<K, V>::clear(){
    if (head == nullptr) {
        return;
    }
    HashNode<K, V>* current = head;
    HashNode<K, V>* next = nullptr;
    while (current != nullptr) {
        next = current->nextNode;
        delete current;
        current = next;
    }
    head = nullptr;
    tail = nullptr;
    size = 0;
}

template<typename T> struct is_vector :std::false_type {};
template <typename... Args> struct is_vector < std::vector<Args...>> :std::true_type {};

template <typename K, typename V>
void LinkedList<K, V>::print() const{
    if (head == nullptr) {
        return;
    }
    HashNode<K, V>* temp = head;
    while (temp != nullptr) {
        std::cout << "key: " << temp->key;
        if constexpr (is_vector<V>::value) {
            std::cout << "termID " << temp->key;
            for (int i = 0; i < temp->value.size(); i++) {
                std::cout << ", docID " << temp->value.at(i).documentID << ", pos are ";
                for (int j = 0; j < temp->value.at(i).positions.size(); j++) {
                    std::cout << temp->value.at(i).positions.at(j) << " ";
                }
            }
        }
        else { std::cout << " val: " << temp->value; }
        std::cout << std::endl;
        temp = temp->nextNode;
    }
}

template <typename K, typename V>
void LinkedList<K, V>::push_back(K key, V value){
    HashNode<K, V>* newNode = new HashNode<K, V>();
    newNode->key = std::move(key);
    newNode->value = std::move(value);
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

template <typename K, typename V>
void LinkedList<K, V>::push_front(K key, V value){
    if (head == nullptr) {
        push_back(std::move(key), std::move(value));
        return;
    }
    HashNode<K, V>* newNode = new HashNode<K, V>();
    newNode->key = std::move(key);
    newNode->value = std::move(value);
    newNode->nextNode = head;
    head = newNode;
    size++;
}

template <typename K, typename V>
bool LinkedList<K, V>::pop_back() {
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
    HashNode<K, V>* temp = head;
    while (temp->nextNode->nextNode != nullptr) {
        temp = temp->nextNode;
    }
    temp->nextNode = nullptr;
    delete tail;
    tail = temp;
    size--;
    return true;
}

template <typename K, typename V>
bool LinkedList<K, V>::pop_front() {
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
    HashNode<K, V>* temp = head;
    head = temp->nextNode;
    delete temp;
    size--;
    return true;
}

template <typename K, typename V>
V* LinkedList<K, V>::get(const K& key) {
    HashNode<K, V>* node = findNode(key);
    if (node) return &node->value;
    return nullptr;
}

template <typename K, typename V>
HashNode<K, V>* LinkedList<K, V>::findNode(const K& key) const {
    if (head == nullptr) {
        return nullptr;
    }
    HashNode<K, V>* temp = head;
    while (temp != nullptr) {
        if (temp->key == key) {
            return temp;
        }
        temp = temp->nextNode;
    }
    return nullptr;
}

template <typename K, typename V>
HashNode<K, V>* LinkedList<K, V>::find(const K& key) {
    HashNode<K, V>* needed = head;
    while (needed != nullptr) {
        if (needed->key == key) {
            return needed;
        }
        needed = needed->nextNode;
    }
    return nullptr;
}

template <typename K, typename V>
bool LinkedList<K, V>::remove(const K& key){
    if(!head){
        return false;
    }
    if(head->key == key){
        HashNode<K, V>* temp = head->nextNode;
        delete head;
        head = temp;
        if (head == nullptr) { // there was one elem
            tail = nullptr;
        }
        size--;
        return true;
    }
    HashNode<K, V>* current = head;
    while(current->nextNode != nullptr){
        if(current->nextNode->key == key){
            HashNode<K, V>* temp = current->nextNode;
            current->nextNode = temp->nextNode;
            if(current->nextNode == nullptr){
                tail = current;
            }
            delete temp;
            size--;
            return true;
        }
        current = current->nextNode;
    }
    return false;
}

template <typename K, typename V>
void LinkedList<K, V>::moveToFront(const K& key){
    if(!head) return;
    if(head->key == key) return;
    HashNode<K, V>* previous = head;
    HashNode<K, V>* current = head->nextNode;
    while(current != nullptr){
        if(current->key == key){
            previous->nextNode = current->nextNode;
            if (current == tail) {
                tail = previous;
            }
            current->nextNode = head;
            head = current;
            return;
        }
        previous = current;
        current = current->nextNode;
    }
}

#endif // LINKEDLIST_H