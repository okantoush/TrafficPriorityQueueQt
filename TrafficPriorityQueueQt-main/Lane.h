#ifndef LANE_H
#define LANE_H

#include "Node.h"
// To create lanes for intersection (N S E W)
class Lane {
private:
    Node* front;
    Node* rear;
    int size;

public:
    Lane() {
        front = rear = nullptr;
        size = 0;
    }

    void enqueue(Node* car) {
        if (!rear) {
            front = rear = car;
        } else {
            rear->next = car;
            rear = car;
        }
        size++;
    }

    Node* dequeue() {
        if (!front) return nullptr;

        Node* temp = front;
        front = front->next;
        if (!front) rear = nullptr;

        temp->next = nullptr;
        size--;
        return temp;
    }

    bool isEmpty() const {
        return front == nullptr;
    }

    int getSize() {
        return size;
    }
};

#endif
