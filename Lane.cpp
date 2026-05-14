#include "Lane.h"

Lane::Lane()
    : front(nullptr), rear(nullptr), size(0)
{
}

void Lane::enqueue(Node* car) {
    if (!rear) {
        front = rear = car;
    } else {
        rear->next = car;
        rear = car;
    }
    size++;
}

Node* Lane::dequeue() {
    if (!front) return nullptr;

    Node* temp = front;
    front = front->next;
    if (!front) rear = nullptr;

    temp->next = nullptr;
    size--;
    return temp;
}

bool Lane::isEmpty() const {
    return front == nullptr;
}

int Lane::getSize() {
    return size;
}
