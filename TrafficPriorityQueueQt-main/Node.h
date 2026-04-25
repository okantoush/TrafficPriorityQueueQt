#ifndef NODE_H
#define NODE_H

#include <QString>
// Create node as car
class Node {
public:
    QString vehicleID;
    bool isEmergency;
    bool willTurnLeft;   // true = this car wants to turn left at the intersection
    int priority;
    Node* next;

    Node(const QString& id = "", bool emergency = false, bool turnLeft = false)
        : vehicleID(id),
          isEmergency(emergency),
          willTurnLeft(turnLeft),
          priority(emergency ? 1 : 0),
          next(nullptr) {}
};

#endif // NODE_H
