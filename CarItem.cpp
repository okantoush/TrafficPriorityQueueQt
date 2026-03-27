#include "CarItem.h"
#include <QBrush>

static const qreal CAR_W = 16;
static const qreal CAR_H = 28;

CarItem::CarItem(Node* node, int dir, int lane)
{
    data          = node;
    direction     = dir;
    laneIndex     = lane;
    stopCoord     = 0;
    clearCoord    = 0;
    atStopLine    = false;
    released      = false;
    inIntersection = false;

    if (dir == 0 || dir == 2)
        setRect(0, 0, CAR_W, CAR_H); // N/S: narrow and tall
    else
        setRect(0, 0, CAR_H, CAR_W); // E/W: wide and short

    setBrush(node->isEmergency ? Qt::red : Qt::blue);
}

bool CarItem::moveForward() {
    const qreal speed = 3;

    switch (direction) {

    case 0: { // North — travels UP — y decreases — leading edge = top = y()
        qreal nextY = y() - speed;

        // Check if car has entered the intersection (passed clearCoord going up)
        if (!inIntersection && y() <= clearCoord)
            inIntersection = true;

        // If not yet released and not already committed, stop at stop line
        if (!released && !inIntersection) {
            if (nextY <= stopCoord) {
                setPos(x(), stopCoord);
                atStopLine = true;
                return false;
            }
        }

        setPos(x(), nextY);
        return (y() + CAR_H) < 0;
    }

    case 1: { // East — travels RIGHT — x increases — leading edge = right = x() + CAR_H
        qreal nextX     = x() + speed;
        qreal leadingX  = x() + CAR_H;

        if (!inIntersection && leadingX >= clearCoord)
            inIntersection = true;

        if (!released && !inIntersection) {
            if ((nextX + CAR_H) >= stopCoord) {
                setPos(stopCoord - CAR_H, y());
                atStopLine = true;
                return false;
            }
        }

        setPos(nextX, y());
        return x() > 640;
    }

    case 2: { // South — travels DOWN — y increases — leading edge = bottom = y() + CAR_H
        qreal nextY      = y() + speed;
        qreal leadingY   = y() + CAR_H;

        if (!inIntersection && leadingY >= clearCoord)
            inIntersection = true;

        if (!released && !inIntersection) {
            if ((nextY + CAR_H) >= stopCoord) {
                setPos(x(), stopCoord - CAR_H);
                atStopLine = true;
                return false;
            }
        }

        setPos(x(), nextY);
        return y() > 640;
    }

    case 3: { // West — travels LEFT — x decreases — leading edge = left = x()
        qreal nextX = x() - speed;

        if (!inIntersection && x() <= clearCoord)
            inIntersection = true;

        if (!released && !inIntersection) {
            if (nextX <= stopCoord) {
                setPos(stopCoord, y());
                atStopLine = true;
                return false;
            }
        }

        setPos(nextX, y());
        return (x() + CAR_H) < 0;
    }

    }
    return false;
}
