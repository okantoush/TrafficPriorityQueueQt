#ifndef NODE_H
#define NODE_H

#include <QString>

class Node {
public:
    QString vehicleID;
    int priority;
    bool isEmergency;
    bool willTurnLeft;
    Node* next;

    Node(const QString& id, bool emergency = false, bool turnLeft = false, int p = -1)
        : vehicleID(id),
          priority((p >= 0) ? p : (emergency ? 100 : 1)),
          isEmergency(emergency),
          willTurnLeft(turnLeft),
          next(nullptr) {}
};

#endif // NODE_H
