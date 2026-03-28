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
    effectiveStop = 0;
    clearCoord    = 0;
    atStopLine     = false;
    released       = false;
    inIntersection = false;
    lateralTarget  = 0;
    originalLateral = 0;
    yielding       = false;

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

        if (!inIntersection && y() <= clearCoord)
            inIntersection = true;

        // Emergency cars never stop at the stop line — they bypass traffic
        if (!released && !inIntersection && !data->isEmergency) {
            if (nextY <= effectiveStop) {
                setPos(x(), effectiveStop);
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

        if (!released && !inIntersection && !data->isEmergency) {
            if ((nextX + CAR_H) >= effectiveStop) {
                setPos(effectiveStop - CAR_H, y());
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

        if (!released && !inIntersection && !data->isEmergency) {
            if ((nextY + CAR_H) >= effectiveStop) {
                setPos(x(), effectiveStop - CAR_H);
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

        if (!released && !inIntersection && !data->isEmergency) {
            if (nextX <= effectiveStop) {
                setPos(effectiveStop, y());
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

void CarItem::animateLateral() {
    const qreal lateralSpeed = 2.0;

    // For N/S cars, lateral = x. For E/W cars, lateral = y.
    bool vertical = (direction == 0 || direction == 2);
    qreal current = vertical ? x() : y();
    qreal diff = lateralTarget - current;

    if (qAbs(diff) < 0.5) return; // close enough

    qreal step = (diff > 0) ? qMin(lateralSpeed, diff) : qMax(-lateralSpeed, diff);

    if (vertical)
        setPos(x() + step, y());
    else
        setPos(x(), y() + step);
}
