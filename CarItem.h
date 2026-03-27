#ifndef CARITEM_H
#define CARITEM_H

#include <QGraphicsRectItem>
#include "Node.h"

class CarItem : public QGraphicsRectItem {
public:
    Node* data;
    int   direction;       // 0=N, 1=E, 2=S, 3=W
    int   laneIndex;       // 0, 1, 2
    qreal stopCoord;       // leading edge stops here if red
    qreal clearCoord;      // once leading edge passes this, car is committed — never stops again
    bool  atStopLine;      // true when car is waiting at stopCoord
    bool  released;        // true once controller granted permission to go
    bool  inIntersection;  // true once car has crossed clearCoord — ignores lights

    CarItem(Node* node, int dir, int lane);

    // Drive one step. Returns true when fully off screen (safe to delete).
    bool moveForward();
};

#endif
