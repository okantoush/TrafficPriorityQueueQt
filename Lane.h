#ifndef LANE_H
#define LANE_H

#include "node.h"

// FIFO queue of cars (one per direction: N, E, S, W).
class Lane {
private:
    Node* front;
    Node* rear;
    int   size;

public:
    Lane();

    void  enqueue(Node* car);
    Node* dequeue();
    bool  isEmpty() const;
    int   getSize();
};

#endif
