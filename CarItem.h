#ifndef CARITEM_H
#define CARITEM_H

#include <QGraphicsRectItem>
#include <QPointF>
#include "Node.h"

class CarItem : public QGraphicsRectItem {
public:
    Node* data;
    int   direction;       // 0=N, 1=E, 2=S, 3=W (updates to destDirection after left turn)
    int   laneIndex;       // 0, 1 (for straight cars); unused for turn cars
    qreal stopCoord;       // leading edge stops here if red
    qreal effectiveStop;   // actual stop position (accounts for car ahead)
    qreal clearCoord;      // once leading edge passes this, car is committed — never stops again
    bool  atStopLine;      // true when car is waiting at stopCoord
    bool  released;        // true once controller granted permission to go
    bool  inIntersection;  // true once car has crossed clearCoord — ignores lights

    // Lane splitting for emergency vehicles
    qreal lateralTarget;   // target x (N/S) or y (E/W) for lateral animation
    qreal originalLateral; // original lane x or y to return to after split
    bool  yielding;        // true when yielding to emergency vehicle

    // Left-turn state
    bool    willTurnLeft;      // cached from data->willTurnLeft
    bool    turning;           // currently mid-turn following the arc
    bool    turnCompleted;     // true once the left turn has finished
    qreal   turnProgress;      // 0 → 1 across the Bezier arc
    QPointF turnP0, turnP1, turnP2;   // Bezier control points for the turn
    int     destDirection;     // direction the car ends up after the turn
    int     blinkTick;         // counter for blinker animation
    QGraphicsRectItem* blinker;  // small flashing left-turn indicator

    // Post-turn merge delay: after finishTurn() the car keeps its lateral
    // position (in the turn-lane it exited into) for mergeDelayTicks ticks,
    // then switches lateralTarget to mergeTargetLateral so animateLateral()
    // slides it over into the straight lane.
    int   mergeDelayTicks;
    qreal mergeTargetLateral;

    CarItem(Node* node, int dir, int lane);

    // Drive one step. Returns true when fully off screen (safe to delete).
    bool moveForward();

    // Animate lateral (sideways) movement toward lateralTarget. Call each tick.
    void animateLateral();

    // Flash the blinker each tick while the car still intends to turn.
    void updateBlinker();

private:
    void startTurn();          // begin Bezier arc when entering intersection
    void finishTurn();         // called once arc completes
};

#endif
