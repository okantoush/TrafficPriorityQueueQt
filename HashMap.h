#ifndef HASHMAP_H
#define HASHMAP_H

// Bucketed hash map: key→int. Used for historical congestion peaks per lane
// and cars-cleared counts per direction.

struct HashNode {
    int       key;
    int       value;
    HashNode* next;

    HashNode(int k, int v);
};

class HashMap {
private:
    // There are 4 lanes so we use a table size of 8 (no expected collisions).
    static const int TABLE_SIZE = 8;

    HashNode* table[TABLE_SIZE];

    int hashFunction(int key) const;

public:
    HashMap();
    ~HashMap();

    // Insert or update a value.
    void put(int key, int value);

    // Retrieve a value (returns 0 if key not found).
    int  get(int key) const;

    // Convenience: put(key, get(key) + 1).
    void increment(int key);
};

#endif
