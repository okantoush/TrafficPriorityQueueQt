#include "HashMap.h"

HashNode::HashNode(int k, int v)
    : key(k), value(v), next(nullptr)
{
}

HashMap::HashMap() {
    for (int i = 0; i < TABLE_SIZE; i++) {
        table[i] = nullptr;
    }
}

HashMap::~HashMap() {
    for (int i = 0; i < TABLE_SIZE; i++) {
        HashNode* entry = table[i];
        while (entry != nullptr) {
            HashNode* prev = entry;
            entry = entry->next;
            delete prev;
        }
        table[i] = nullptr;
    }
}

int HashMap::hashFunction(int key) const {
    return key % TABLE_SIZE;
}

void HashMap::put(int key, int value) {
    int hashVal = hashFunction(key);
    HashNode* entry = table[hashVal];

    while (entry != nullptr) {
        if (entry->key == key) {
            entry->value = value;   // Update existing
            return;
        }
        entry = entry->next;
    }

    // Key not found — insert at the front of the bucket's linked list.
    HashNode* newNode = new HashNode(key, value);
    newNode->next = table[hashVal];
    table[hashVal] = newNode;
}

int HashMap::get(int key) const {
    int hashVal = hashFunction(key);
    HashNode* entry = table[hashVal];

    while (entry != nullptr) {
        if (entry->key == key) {
            return entry->value;
        }
        entry = entry->next;
    }
    return 0;
}

void HashMap::increment(int key) {
    put(key, get(key) + 1);
}
