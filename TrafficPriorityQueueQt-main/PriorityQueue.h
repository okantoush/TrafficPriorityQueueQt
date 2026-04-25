#ifndef PRIORITYQUEUE_H
#define PRIORITYQUEUE_H

#include "Node.h"
// Queue for emergency vehicles
class PriorityQueue {
private:
    Node* front;

public:
    PriorityQueue();
    ~PriorityQueue();

    void enqueue(Node* newNode);
    Node* dequeue();
    Node* peek() const;
    bool isEmpty() const;
};

#endif // PRIORITYQUEUE_H
