#ifndef HASHMAP_H
#define HASHMAP_H

struct HashNode {
    int key;
    int value;
    HashNode* next;

    HashNode(int k, int v) : key(k), value(v), next(nullptr) {}
};

class HashMap {
private:
    static const int TABLE_SIZE = 8; // Small size is fine for 4 directions
    HashNode* table[TABLE_SIZE];

    // Basic hash function
    int hashFunction(int key) const {
        return key % TABLE_SIZE;
    }

public:
    HashMap() {
        for (int i = 0; i < TABLE_SIZE; i++) {
            table[i] = nullptr;
        }
    }

    ~HashMap() {
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

    // Insert or update a value
    void put(int key, int value) {
        int hashVal = hashFunction(key);
        HashNode* entry = table[hashVal];

        while (entry != nullptr) {
            if (entry->key == key) {
                entry->value = value; // Update existing
                return;
            }
            entry = entry->next;
        }

        // Key not found, insert at the front of the linked list
        HashNode* newNode = new HashNode(key, value);
        newNode->next = table[hashVal];
        table[hashVal] = newNode;
    }

    // Retrieve a value (returns 0 if not found)
    int get(int key) const {
        int hashVal = hashFunction(key);
        HashNode* entry = table[hashVal];

        while (entry != nullptr) {
            if (entry->key == key) {
                return entry->value;
            }
            entry = entry->next;
        }
        return 0; // Default if key doesn't exist
    }

    // Helper to increment statistics easily
    void increment(int key) {
        put(key, get(key) + 1);
    }
};

#endif
