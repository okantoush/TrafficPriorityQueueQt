#ifndef CARITEM_H
#define CARITEM_H

#include <QGraphicsRectItem>
#include <QPointF>
#include "node.h"
#include "edge.h"

class CarItem : public QGraphicsRectItem {
public:
    Node* data;
    int   direction;       // 0=N, 1=E, 2=S, 3=W (updates to destDirection after left turn)
    int   laneIndex;       // 0, 1 (for straight cars); unused for turn cars

    // ── Multi-intersection support ────────────────────────────────────
    // originX/originY = top-left scene coords of this car's owning
    // intersection tile. All hardcoded geometry constants in CarItem
    // (Bezier endpoints, exit thresholds, merge targets) are RELATIVE
    // offsets inside a 600×600 tile and must have origin added to land
    // in scene space.
    int   intersectionId;  // 0..N-1 — which intersection this car belongs to
    int   destGraphNodeId;   // exit node id; -1 if not using graph exit removal
    bool  graphRouteExitEnabled; // when true, delete when leading edge passes graphExitScene
    QPointF graphExitScene;      // scene coords of exit node (center), valid when removal runs
    // Graph left-turn: keep removal off until finishTurn(), then exit on departure road perimeter.
    bool    graphPostTurnExitPending;
    QPointF graphPostTurnExitScene;
    qreal originX;
    qreal originY;

    // Off-tile exit thresholds, in scene coords. Defaults are tile-boundary
    // values (set in spawnCarAt). Overrides extend a car's allowed travel
    // through a decorative road stub before deletion (e.g. I1's north stub
    // pushes exitN to the scene top, I3/I4's south stubs push exitS to the
    // scene bottom).
    qreal exitN;
    qreal exitS;
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

    Qt::Edge* currentEdge; //to keep track of cars' current edges
    double t; //to determine how far a car has moved along an edge
    double speed; //to keep track of how fast t increases

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
