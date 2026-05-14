#include "PriorityQueue.h"

PriorityQueue::PriorityQueue() : front(nullptr) {}

PriorityQueue::~PriorityQueue() {
    while (front != nullptr) {
        Node* temp = front;
        front = front->next;
        delete temp;
    }
}

void PriorityQueue::enqueue(Node* newNode) {
    if (!newNode) return;

    // Insert based on priority (higher value first)
    if (front == nullptr || newNode->priority > front->priority) {
        newNode->next = front;
        front = newNode;
    } else {
        Node* temp = front;
        while (temp->next != nullptr && temp->next->priority >= newNode->priority) {
            temp = temp->next;
        }
        newNode->next = temp->next;
        temp->next = newNode;
    }
}

Node* PriorityQueue::dequeue() {
    if (front == nullptr) return nullptr;

    Node* temp = front;
    front = front->next;
    temp->next = nullptr;
    return temp;
}

Node* PriorityQueue::peek() const {
    return front;
}

bool PriorityQueue::isEmpty() const {
    return front == nullptr;
}
