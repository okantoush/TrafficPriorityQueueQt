#ifndef NODE_H
#define NODE_H

#include <QString>

class Node {
public:
    QString vehicleID;
    bool isEmergency;
    int priority;
    Node* next;

    Node(const QString& id = "", bool emergency = false)
        : vehicleID(id),
          isEmergency(emergency),
          priority(emergency ? 1 : 0),
          next(nullptr) {}
};

#endif // NODE_H
