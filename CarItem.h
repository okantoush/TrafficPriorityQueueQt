#ifndef CARITEM_H
#define CARITEM_H

#include <QGraphicsRectItem>
#include <QList>
#include <QPointF>
#include "node.h"

struct GraphInfo;   // forward declared; populated by IntersectionWindow

class CarItem : public QGraphicsRectItem {
public:
    // Shared lookup tables for the current graph. IntersectionWindow sets
    // this pointer when it builds the graph; the static helpers below
    // read from it instead of hardcoded switch statements so the same
    // logic works on any grid size (2×2, 3×3, 3×2, …).
    static GraphInfo* graphInfo;

    Node* data;
    int   direction;       // 0=N, 1=E, 2=S, 3=W (updates to destDirection after left turn)
    int   laneIndex;       // 0, 1 (for straight cars); unused for turn cars

    // ── Dijkstra path-following ───────────────────────────────────────
    // Set once at spawn: scene coords of every node on the route from
    // start to end (start at index 0, end at last). When non-empty,
    // moveForward() ignores the direction/stop logic and drives the
    // car waypoint-by-waypoint at constant speed.
    QList<QPointF> pathWaypoints;
    QList<int>     pathNodeIds;  // graph node ID for each waypoint (same length)
    int            pathCursor;   // index of the NEXT waypoint to reach

    // Set by IntersectionWindow each tick: true when the next waypoint is
    // an intersection-approach node whose traffic light is not green.
    // movePath() consults this when the car arrives at the waypoint: if
    // true, it snaps to pathStopPos (rank-adjusted, see below) and holds
    // without advancing; if false, it advances and reorients normally.
    bool    stopAtNextWaypoint;

    // Set by IntersectionWindow each tick alongside stopAtNextWaypoint: the
    // actual point in scene coords the car should stop at. For the front car
    // in a queue this equals the approach waypoint; cars behind it get
    // shifted further back along the direction of travel so they stack with
    // proper spacing instead of all piling onto the same waypoint.
    QPointF pathStopPos;

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

    // Turn state. `turning` + `turnProgress` + control points are reused
    // for the path-mode Bezier (set up by startPathTurn(), stepped each
    // tick from movePath, torn down by finishPathTurn()).
    bool    willTurnLeft;      // legacy spawn flag, currently unused visually
    bool    turning;           // true while a Bezier turn is in progress
    bool    turnCompleted;     // legacy (non-path mode only)
    qreal   turnProgress;      // 0 → 1 across the Bezier arc
    QPointF turnP0, turnP1, turnP2;   // Bezier control points
    int     destDirection;     // legacy
    bool    turnIsRight;       // true = clockwise (+90°), false = CCW (-90°)
    qreal   turnSpeed;         // turnProgress increment per tick (= 3/curveLen)

    // ── Blinker state (path-driven) ───────────────────────────────────
    // Every car has two small yellow rectangles, one on each front
    // corner. The blinker on the side the car is about to turn toward
    // flashes; the other stays hidden. turnIntent is recomputed each
    // tick from the upcoming/current path segment: 0=none, 1=left, 2=right.
    int                 blinkTick;
    int                 turnIntent;
    QGraphicsRectItem*  blinkerLeft;
    QGraphicsRectItem*  blinkerRight;

    // Post-turn merge delay: after finishTurn() the car keeps its lateral
    // position (in the turn-lane it exited into) for mergeDelayTicks ticks,
    // then switches lateralTarget to mergeTargetLateral so animateLateral()
    // slides it over into the straight lane.
    int   mergeDelayTicks;
    qreal mergeTargetLateral;

    CarItem(Node* node, int dir, int lane);

    // Drive one step. Returns true when the car has finished its route or
    // run off the world (safe to delete).
    bool moveForward();

    // Animate lateral (sideways) movement toward lateralTarget. Call each tick.
    void animateLateral();

    // Flash the blinker each tick while the car still intends to turn.
    void updateBlinker();

    // Convenience: scene-space center of the car's current rect.
    QPointF sceneCenter() const;
    // Place the car so its rect is centered on `c`.
    void    setSceneCenter(const QPointF& c);
    // Update sprite shape + rotation pivot for a new direction (0=N..3=W).
    void    setOrientation(int dir);

    // ── Path / graph utilities (statics) ──────────────────────────────
    // Map an "approach" graph node (the last node before an intersection
    // in each of the 4 approach directions) to its intersection and dir.
    // Returns false if the node ID isn't one of the 16 approach nodes.
    static bool approachNodeInfo(int nodeId, int& outIntId, int& outDir);
    // Map a graph node to its directed-lane chain (0..7). -1 if unknown.
    static int  nodeChain(int nodeId);
    // The cardinal direction (0=N..3=W) each lane chain heads in.
    static int  chainDirection(int chainId);
    // Given an approach node, the next node on the route, and the approach
    // direction, return the car's intent at that intersection:
    //   0 = straight, 1 = left turn, 2 = right turn.
    static int  turnIntentForApproach(int approachId, int nextId, int approachDir);
    // Lateral offset (in scene units) that shifts the graph-node position
    // to a specific lane center, for a car driving in `direction`.
    // dir: 0=N, 1=E, 2=S, 3=W. lane: 0=turn-only, 1=inner straight,
    // 2=outer (curb side / right-turn lane). Returns 0 for invalid input.
    static qreal laneOffsetForDirAndLane(int dir, int lane);

    // ── Path inspection (instance methods) ────────────────────────────
    // The cardinal direction the car has at waypoint `idx`. Picks the
    // surrounding axis-aligned segment (incoming preferred, since that's
    // the PRE-turn direction at turn-edge waypoints).
    int waypointDirection(int idx) const;
    // The lane (0/1/2) the car should be in approaching waypoint `idx`,
    // based on the intent at the NEXT approach node at or after `idx`.
    int waypointLane(int idx) const;
    // Shift every entry of pathWaypoints to its lane center, in place.
    // Call after the spawn populates pathWaypoints/pathNodeIds and before
    // the car starts driving.
    void applyLaneOffsets();

private:
    void startTurn();          // legacy non-path Bezier
    void finishTurn();         // legacy non-path Bezier

    // Path-mode turn (used by movePath). Sets up turnP0/P1/P2 with the
    // tangent control point at the L-elbow between the pre- and post-turn
    // axis-aligned directions, picks turnIsRight from the direction change,
    // and computes turnSpeed so the Bezier traversal matches normal driving
    // speed (3 px/tick) along the control-polygon length.
    void startPathTurn();
    // Called at turnProgress >= 1: snap to turnP2, swap rect to post
    // direction (resetting rotation), advance pathCursor past the
    // post-diagonal waypoint, clear turning state.
    void finishPathTurn();
    // True if the path edge from pathNodeIds[fromIdx] to [toIdx] is a turn
    // edge in the graph (the source is an approach node and the
    // destination is in a different chain).
    bool isDiagonalSegment(int fromIdx, int toIdx) const;

    // Path-following: drive toward pathWaypoints[pathCursor], advance the
    // cursor when reached. Returns true once the last waypoint is consumed.
    bool movePath();
    // Look ahead from waypoint `fromIdx` to find the next axis-aligned
    // segment's direction (N/E/S/W). Diagonal turn edges (where |dx|==|dy|)
    // inherit the direction of the following straight segment so cars
    // visually "anticipate" the turn instead of pointing the old way.
    int  segmentDirection(int fromIdx) const;

    // Inspect the path around pathCursor to decide whether the car is
    // currently mid-turn or approaching a turn, and return left/right/none.
    int  computeTurnIntent() const;

    // (Re)place blinkerLeft/blinkerRight relative to the car's rect for
    // the current direction. Called from constructor and setOrientation.
    void positionBlinkers();
};

#endif
