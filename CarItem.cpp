#include "CarItem.h"
#include "graphinfo.h"
#include <QBrush>
#include <QPen>
#include <QtMath>

// Shared graph lookup tables. IntersectionWindow assigns the address of
// its m_graphInfo into this when the graph is built; reset to nullptr on
// scene rebuild.
GraphInfo* CarItem::graphInfo = nullptr;

static const qreal CAR_W = 16;
static const qreal CAR_H = 28;

// Blinker dimensions — small yellow square on the left side of the car
static const qreal BLINK = 5;

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
    destGraphNodeId = -1;
    graphRouteExitEnabled = false;
    graphExitScene    = QPointF(0, 0);
    graphPostTurnExitPending = false;
    graphPostTurnExitScene   = QPointF(0, 0);

    // Left-turn state
    willTurnLeft   = node->willTurnLeft;
    turning        = false;
    turnCompleted  = false;
    turnProgress   = 0.0;
    blinkTick      = 0;
    mergeDelayTicks    = 0;
    mergeTargetLateral = 0;
    intersectionId     = 0;
    originX            = 0;
    originY            = 0;
    exitN              = 0;     // matches default originY; overwritten in spawnCarAt
    exitS              = 640;   // matches default originY+640; overwritten in spawnCarAt
    pathCursor         = 0;
    stopAtNextWaypoint = false;
    pathStopPos        = QPointF(0, 0);
    turnIntent         = 0;
    turnIsRight        = false;
    turnSpeed          = 0.025;
    blinkerLeft        = nullptr;
    blinkerRight       = nullptr;
    // Left turn: N→W, E→N, S→E, W→S  ==  (dir + 3) % 4
    destDirection  = willTurnLeft ? (dir + 3) % 4 : dir;

    // Rect shape + rotation pivot (at rect center) so the car can be rotated
    // around its center during the left-turn animation.
    if (dir == 0 || dir == 2) {
        setRect(0, 0, CAR_W, CAR_H); // N/S: narrow and tall
        setTransformOriginPoint(CAR_W / 2.0, CAR_H / 2.0);
    } else {
        setRect(0, 0, CAR_H, CAR_W); // E/W: wide and short
        setTransformOriginPoint(CAR_H / 2.0, CAR_W / 2.0);
    }

    // Color: emergencies are red, every other car is the same dark blue.
    // (The previous "light-blue distinct left-turner" color is gone —
    // under Dijkstra path-following, turning is determined by the route
    // rather than a per-car flag, so visually all cars are equal.)
    if (node->isEmergency)
        setBrush(Qt::red);
    else
        setBrush(QColor(20, 60, 180));

    // Z above the graph-overlay nodes/edges so cars aren't hidden behind them.
    setZValue(15);

    // Every car gets both blinkers; visibility is driven by turnIntent.
    blinkerLeft = new QGraphicsRectItem(this);
    blinkerLeft->setBrush(QColor(255, 200, 0));
    blinkerLeft->setPen(QPen(QColor(160, 110, 0), 0.5));
    blinkerLeft->setVisible(false);

    blinkerRight = new QGraphicsRectItem(this);
    blinkerRight->setBrush(QColor(255, 200, 0));
    blinkerRight->setPen(QPen(QColor(160, 110, 0), 0.5));
    blinkerRight->setVisible(false);

    positionBlinkers();
}

// ── Begin the Bezier turn arc ───────────────────────────────────────────
// Called automatically when a left-turning car first crosses clearCoord.
// Bezier is computed in CENTER coords so setRotation stays aligned with the
// path. P0 = car's current center, P1 = tangent corner, P2 = destination
// center just past the intersection in the NEW heading.
void CarItem::startTurn()
{
    turning        = true;
    turnProgress   = 0.0;

    // Car's current CENTER in scene coords.
    bool vertical = (direction == 0 || direction == 2);
    qreal cx_off = vertical ? CAR_W / 2.0 : CAR_H / 2.0;
    qreal cy_off = vertical ? CAR_H / 2.0 : CAR_W / 2.0;
    turnP0 = QPointF(x() + cx_off, y() + cy_off);

    // Exit CENTER = leftmost lane on the destination road — the "empty third
    // lane" adjacent to the median. This coordinate mirrors the approach-side
    // turn lane from the opposite direction (always empty on the exit side).
    // Values below are scene-space CENTERS (matching the 150-px-wide roads
    // with intersection at (225,225)–(375,375)) and the re-slotted lane layout
    // (turn / inner / corridor / outer):
    //   After N→W: center y = 289  (= TURN_W 281 + 8 half-height)
    //   After E→N: center x = 311  (= TURN_N 303 + 8)
    //   After S→E: center y = 311  (= TURN_E 303 + 8)
    //   After W→S: center x = 289  (= TURN_S 281 + 8)
    // Tile-relative offsets are added to (originX, originY) so the same
    // logic works across multiple intersections in the scene.
    switch (direction) {
    case 0: // N → W:  enters heading up, exits heading left
        turnP1 = QPointF(turnP0.x(),     originY + 289);
        turnP2 = QPointF(originX + 190, originY + 289);
        break;
    case 1: // E → N
        turnP1 = QPointF(originX + 311, turnP0.y());
        turnP2 = QPointF(originX + 311, originY + 190);
        break;
    case 2: // S → E
        turnP1 = QPointF(turnP0.x(),     originY + 311);
        turnP2 = QPointF(originX + 410, originY + 311);
        break;
    case 3: // W → S
        turnP1 = QPointF(originX + 289, turnP0.y());
        turnP2 = QPointF(originX + 289, originY + 410);
        break;
    }
}

void CarItem::finishTurn()
{
    turning        = false;
    turnCompleted  = true;
    inIntersection = true;   // already through — don't stop at any more lines
    released       = true;

    // Scene CENTER at the end of the Bezier.
    QPointF sceneCenter = turnP2;

    // Direction updates to the destination heading.
    direction = destDirection;

    // Resize rect + move rotation pivot to match the new orientation. Then zero
    // rotation — the new rect shape already represents the heading, so we don't
    // need the visual rotation anymore.
    bool vertical = (direction == 0 || direction == 2);
    if (vertical) {
        setRect(0, 0, CAR_W, CAR_H);
        setTransformOriginPoint(CAR_W / 2.0, CAR_H / 2.0);
    } else {
        setRect(0, 0, CAR_H, CAR_W);
        setTransformOriginPoint(CAR_H / 2.0, CAR_W / 2.0);
    }
    setRotation(0);

    // Place top-left so scene center = P2 (keeps visual continuity with the
    // rotated-rect position on the last turn frame — no jump).
    QPointF o = transformOriginPoint();
    setPos(sceneCenter.x() - o.x(), sceneCenter.y() - o.y());

    // The car just exited into the leftmost (turn-lane) position on the
    // destination road. Don't merge immediately — let the car drive visibly in
    // the turn lane for a bit first, then slide over into the adjacent inner
    // straight lane. animateLateral() ticks mergeDelayTicks down each frame
    // and only then sets lateralTarget to mergeTargetLateral.
    //
    // Top-left convention, matching LANE_* values in IntersectionWindow.cpp
    // (lane centers, not edges):
    //   N inner straight lane: x = 326  (LANE_N[0])
    //   E inner straight lane: y = 326  (LANE_E[0])
    //   S inner straight lane: x = 258  (LANE_S[1])
    //   W inner straight lane: y = 258  (LANE_W[1])
    bool vert = (direction == 0 || direction == 2);
    qreal currentLateral = vert ? x() : y();
    lateralTarget   = currentLateral;   // hold position while still in turn lane
    originalLateral = currentLateral;

    switch (direction) {
    case 0: mergeTargetLateral = originX + 326; break;
    case 1: mergeTargetLateral = originY + 326; break;
    case 2: mergeTargetLateral = originX + 258; break;
    case 3: mergeTargetLateral = originY + 258; break;
    }
    mergeDelayTicks = 35;   // ~1.75s at 50ms/tick: visible "drive in turn lane"

    // Graph-route left turn: begin deleting only after departure heading matches exit road.
    if (graphPostTurnExitPending) {
        graphExitScene            = graphPostTurnExitScene;
        graphRouteExitEnabled     = true;
        graphPostTurnExitPending  = false;
    }

    // (Old single-blinker cleanup removed — under path-following the blinker
    //  state lives in updateBlinker()/turnIntent, and finishTurn() is only
    //  reachable from the legacy non-path Bezier branch which is unused by
    //  Dijkstra routes anyway.)
}

bool CarItem::moveForward() {
    // ── Dijkstra path mode ─────────────────────────────────────────────
    // If the spawn handed us a route, drive it waypoint-by-waypoint and
    // ignore the old direction-based stop logic entirely. Lights are
    // bypassed for now — path-following is the primary contract.
    if (!pathWaypoints.isEmpty()) {
        return movePath();
    }

    const qreal speed = 3;

    // ── TURNING MODE: follow the Bezier arc ──────────────────────────
    // Bezier control points are CENTER coords. We place the item so that the
    // rotated rect's scene-center lands on the Bezier center, and rotate from
    // 0° at entry to -90° at exit (left turn = counter-clockwise on screen).
    if (turning) {
        turnProgress += 0.025;   // ~40 ticks to complete
        if (turnProgress >= 1.0) {
            finishTurn();
            return false;
        }
        qreal t = turnProgress;
        qreal u = 1.0 - t;
        QPointF center = u*u*turnP0 + 2*u*t*turnP1 + t*t*turnP2;

        // Scene center (under rotation around transformOriginPoint) = pos + origin.
        // So pos = center - origin to put the rotated rect's center on the Bezier.
        QPointF o = transformOriginPoint();
        setPos(center.x() - o.x(), center.y() - o.y());

        // Left turn = 90° counter-clockwise on-screen = negative angle in Qt.
        setRotation(-90.0 * t);
        return false;
    }

    switch (direction) {

    case 0: { // North — travels UP — y decreases — leading edge = top = y()
        qreal nextY = y() - speed;

        if (!inIntersection && y() <= clearCoord) {
            inIntersection = true;
            if (willTurnLeft && !turnCompleted) {
                startTurn();
                return false;
            }
        }

        // Emergency cars never stop at the stop line — they bypass traffic
        if (!released && !inIntersection && !data->isEmergency) {
            if (nextY <= effectiveStop) {
                setPos(x(), effectiveStop);
                atStopLine = true;
                return false;
            }
        }

        setPos(x(), nextY);
        if (destGraphNodeId >= 0 && willTurnLeft && !turnCompleted)
            return false;
        if (graphRouteExitEnabled) {
            const qreal m = 8.0;
            return y() < graphExitScene.y() - m;
        }
        return (y() + CAR_H) < exitN;   // exited the car's northbound road end
    }

    case 1: { // East — travels RIGHT — x increases — leading edge = right = x() + CAR_H
        qreal nextX     = x() + speed;
        qreal leadingX  = x() + CAR_H;

        if (!inIntersection && leadingX >= clearCoord) {
            inIntersection = true;
            if (willTurnLeft && !turnCompleted) {
                startTurn();
                return false;
            }
        }

        if (!released && !inIntersection && !data->isEmergency) {
            if ((nextX + CAR_H) >= effectiveStop) {
                setPos(effectiveStop - CAR_H, y());
                atStopLine = true;
                return false;
            }
        }

        setPos(nextX, y());
        if (destGraphNodeId >= 0 && willTurnLeft && !turnCompleted)
            return false;
        if (graphRouteExitEnabled) {
            const qreal m = 8.0;
            return (x() + CAR_H) > graphExitScene.x() + m;
        }
        return x() > originX + 640;   // exited right edge of this car's tile
    }

    case 2: { // South — travels DOWN — y increases — leading edge = bottom = y() + CAR_H
        qreal nextY      = y() + speed;
        qreal leadingY   = y() + CAR_H;

        if (!inIntersection && leadingY >= clearCoord) {
            inIntersection = true;
            if (willTurnLeft && !turnCompleted) {
                startTurn();
                return false;
            }
        }

        if (!released && !inIntersection && !data->isEmergency) {
            if ((nextY + CAR_H) >= effectiveStop) {
                setPos(x(), effectiveStop - CAR_H);
                atStopLine = true;
                return false;
            }
        }

        setPos(x(), nextY);
        if (destGraphNodeId >= 0 && willTurnLeft && !turnCompleted)
            return false;
        if (graphRouteExitEnabled) {
            const qreal m = 8.0;
            return (y() + CAR_H) > graphExitScene.y() + m;
        }
        return y() > exitS;   // exited the car's southbound road end
    }

    case 3: { // West — travels LEFT — x decreases — leading edge = left = x()
        qreal nextX = x() - speed;

        if (!inIntersection && x() <= clearCoord) {
            inIntersection = true;
            if (willTurnLeft && !turnCompleted) {
                startTurn();
                return false;
            }
        }

        if (!released && !inIntersection && !data->isEmergency) {
            if (nextX <= effectiveStop) {
                setPos(effectiveStop, y());
                atStopLine = true;
                return false;
            }
        }

        setPos(nextX, y());
        if (destGraphNodeId >= 0 && willTurnLeft && !turnCompleted)
            return false;
        if (graphRouteExitEnabled) {
            const qreal m = 8.0;
            return x() < graphExitScene.x() - m;
        }
        return (x() + CAR_H) < originX;   // exited left edge of this car's tile
    }

    }
    return false;
}

void CarItem::animateLateral() {
    // Path-driven cars manage their own motion in movePath(); applying the
    // lateral animation on top would drag them toward lateralTarget=0 each
    // tick (the constructor default), which manifests as a slow drift to
    // x=0 / y=0.
    if (!pathWaypoints.isEmpty()) return;

    const qreal lateralSpeed = 2.0;

    // Post-turn merge delay: keep the car in its turn-exit lane for a beat,
    // then flip lateralTarget to the inner straight lane so the slide below
    // happens on subsequent ticks.
    if (mergeDelayTicks > 0) {
        mergeDelayTicks--;
        if (mergeDelayTicks == 0) {
            lateralTarget   = mergeTargetLateral;
            originalLateral = mergeTargetLateral;
        }
    }

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

// Each tick: refresh the turn intent from the path, then flash whichever
// blinker matches it. If turnIntent is 0 (driving straight), both stay
// hidden. The blinkers' static positions are set by positionBlinkers()
// from setOrientation() — this method only toggles visibility.
void CarItem::updateBlinker() {
    turnIntent = computeTurnIntent();
    blinkTick++;
    bool flashOn = (blinkTick / 10) % 2 == 0;
    if (blinkerLeft)  blinkerLeft->setVisible(turnIntent == 1 && flashOn);
    if (blinkerRight) blinkerRight->setVisible(turnIntent == 2 && flashOn);
}

// Place blinkerLeft / blinkerRight on the car's front-left and front-right
// corners for the current direction. Coordinates are LOCAL to the
// QGraphicsRectItem (the parent car), so they need to be recomputed any
// time the car's rect orientation changes (N/S vs E/W swap).
void CarItem::positionBlinkers() {
    if (!blinkerLeft || !blinkerRight) return;
    switch (direction) {
    case 0:  // N (heading up). Front = top.  Left = west edge, right = east edge.
        blinkerLeft ->setRect(-BLINK - 1,           2,                    BLINK, BLINK);
        blinkerRight->setRect(CAR_W + 1,            2,                    BLINK, BLINK);
        break;
    case 1:  // E (heading right). Front = right. Left = north edge, right = south edge.
        blinkerLeft ->setRect(CAR_H - BLINK - 2,    -BLINK - 1,           BLINK, BLINK);
        blinkerRight->setRect(CAR_H - BLINK - 2,    CAR_W + 1,            BLINK, BLINK);
        break;
    case 2:  // S (heading down). Front = bottom. Left = east edge, right = west edge.
        blinkerLeft ->setRect(CAR_W + 1,            CAR_H - BLINK - 2,    BLINK, BLINK);
        blinkerRight->setRect(-BLINK - 1,           CAR_H - BLINK - 2,    BLINK, BLINK);
        break;
    case 3:  // W (heading left). Front = left. Left = south edge, right = north edge.
        blinkerLeft ->setRect(2,                    CAR_W + 1,            BLINK, BLINK);
        blinkerRight->setRect(2,                    -BLINK - 1,           BLINK, BLINK);
        break;
    }
}

// Decide if the car is at or approaching a turn, and which way (0=none,
// 1=left, 2=right). Uses graph node IDs — robust to the lane-offset
// shift, which can make the diagonal segment's |dx| and |dy| diverge
// enough that a geometry-only check would miss it.
//
// "Approaching" = the next target waypoint is an approach node.
// "Mid-turn"   = the waypoint we just left is an approach node.
// In either case, we know the approach node and the post-intersection
// node that follows it; turnIntentForApproach() compares the two
// chains' directions to decide left vs right.
int CarItem::computeTurnIntent() const {
    if (pathNodeIds.size() < 2 || pathCursor < 1) return 0;

    int approachIdx = -1;
    int approachDir = -1;
    int intId = -1;

    // Mid-turn: we just left an approach.
    if (pathCursor - 1 >= 0 && pathCursor - 1 < pathNodeIds.size() &&
        approachNodeInfo(pathNodeIds[pathCursor - 1], intId, approachDir))
    {
        approachIdx = pathCursor - 1;
    }
    // Otherwise check whether the upcoming waypoint is an approach.
    else if (pathCursor < pathNodeIds.size() &&
             approachNodeInfo(pathNodeIds[pathCursor], intId, approachDir))
    {
        approachIdx = pathCursor;
    }

    if (approachIdx < 0 || approachIdx + 1 >= pathNodeIds.size()) return 0;

    return turnIntentForApproach(pathNodeIds[approachIdx],
                                 pathNodeIds[approachIdx + 1],
                                 approachDir);
}

// ── Static helpers ──────────────────────────────────────────────────────

bool CarItem::approachNodeInfo(int nodeId, int& outIntId, int& outDir) {
    if (!graphInfo) return false;
    auto it = graphInfo->approachNodes.find(nodeId);
    if (it == graphInfo->approachNodes.end()) return false;
    outIntId = it->intId;
    outDir   = it->dir;
    return true;
}

int CarItem::nodeChain(int nodeId) {
    if (!graphInfo) return -1;
    auto it = graphInfo->nodeChain.find(nodeId);
    return (it != graphInfo->nodeChain.end()) ? it.value() : -1;
}

int CarItem::chainDirection(int chainId) {
    if (!graphInfo) return -1;
    auto it = graphInfo->chainDirection.find(chainId);
    return (it != graphInfo->chainDirection.end()) ? it.value() : -1;
}

int CarItem::turnIntentForApproach(int /*approachId*/, int nextId, int approachDir) {
    int nc = nodeChain(nextId);
    if (nc < 0) return 0;
    int nextDir = chainDirection(nc);
    if (nextDir < 0 || approachDir < 0) return 0;
    if (nextDir == approachDir)             return 0;   // straight
    if (nextDir == (approachDir + 3) % 4)   return 1;   // counter-clockwise
    if (nextDir == (approachDir + 1) % 4)   return 2;   // clockwise
    return 0;
}

qreal CarItem::laneOffsetForDirAndLane(int dir, int lane) {
    // Lane: 0 = turn-only (innermost, adjacent to median),
    //       1 = inner straight (default),
    //       2 = outer (curb side, right-turn lane).
    //
    // The graph nodes sit at ±32 from the median. Lane centers (top-left
    // of a 16-wide car + 8) differ from the graph node by these amounts:
    //   N (going up,   east half): turn=311, inner=334, outer=361 vs graph 332
    //   S (going down, west half): turn=289, inner=266, outer=239 vs graph 268
    //   E and W follow the same pattern on y.
    static const qreal offsets[4][3] = {
        { -21,  +2, +29 },   // N
        { -21,  +2, +29 },   // E
        { +21,  -2, -29 },   // S
        { +21,  -2, -29 }    // W
    };
    if (dir < 0 || dir > 3 || lane < 0 || lane > 2) return 0;
    return offsets[dir][lane];
}

// ── Instance methods ────────────────────────────────────────────────────

int CarItem::waypointDirection(int idx) const {
    auto axisDir = [&](int a, int b) -> int {
        if (a < 0 || b < 0 || a >= pathWaypoints.size() || b >= pathWaypoints.size())
            return -1;
        QPointF p1 = pathWaypoints[a];
        QPointF p2 = pathWaypoints[b];
        qreal dx = p2.x() - p1.x();
        qreal dy = p2.y() - p1.y();
        if (qAbs(qAbs(dx) - qAbs(dy)) < 1.0) return -1;   // diagonal
        if (qAbs(dx) > qAbs(dy)) return (dx > 0) ? 1 : 3;
        return (dy > 0) ? 2 : 0;
    };

    // Incoming segment first (gives the PRE direction at pre-diagonal
    // waypoints), then outgoing, then look further forward/back through
    // diagonals if needed.
    int d = axisDir(idx - 1, idx);   if (d >= 0) return d;
    d = axisDir(idx,     idx + 1);   if (d >= 0) return d;
    d = axisDir(idx + 1, idx + 2);   if (d >= 0) return d;
    d = axisDir(idx - 2, idx - 1);   if (d >= 0) return d;
    return -1;
}

int CarItem::waypointLane(int idx) const {
    // Look forward from idx (inclusive) for the next approach node. The
    // intent at THAT approach decides the lane the car drives in along
    // the segment(s) leading up to it.
    for (int i = idx; i + 1 < pathNodeIds.size(); i++) {
        int intId, dir;
        if (!approachNodeInfo(pathNodeIds[i], intId, dir)) continue;
        int intent = turnIntentForApproach(pathNodeIds[i], pathNodeIds[i + 1], dir);
        switch (intent) {
        case 1: return 0;   // left  → turn-only lane
        case 2: return 2;   // right → outer lane
        default: return 1;  // straight → inner lane
        }
    }
    return 1;   // no more approaches on this route → default inner
}

void CarItem::applyLaneOffsets() {
    if (pathWaypoints.size() != pathNodeIds.size()) return;
    for (int i = 0; i < pathWaypoints.size(); i++) {
        int dir = waypointDirection(i);
        if (dir < 0) continue;
        int lane = waypointLane(i);
        qreal off = laneOffsetForDirAndLane(dir, lane);
        if (off == 0) continue;
        bool vertical = (dir == 0 || dir == 2);
        QPointF p = pathWaypoints[i];
        if (vertical) p.setX(p.x() + off);
        else          p.setY(p.y() + off);
        pathWaypoints[i] = p;
    }
}

// ── Path-following helpers ──────────────────────────────────────────────

QPointF CarItem::sceneCenter() const {
    QRectF r = rect();
    return QPointF(x() + r.width() / 2.0, y() + r.height() / 2.0);
}

void CarItem::setSceneCenter(const QPointF& c) {
    QRectF r = rect();
    setPos(c.x() - r.width() / 2.0, c.y() - r.height() / 2.0);
}

// Update the sprite's rect and rotation pivot for a new cardinal direction.
// Called when the path crosses a 90° turn between waypoints so the car
// visually faces the new direction without changing its scene center.
void CarItem::setOrientation(int dir) {
    if (dir < 0 || dir > 3) return;
    QPointF c = sceneCenter();
    direction = dir;
    if (dir == 0 || dir == 2) {
        setRect(0, 0, CAR_W, CAR_H);   // N/S: narrow + tall
        setTransformOriginPoint(CAR_W / 2.0, CAR_H / 2.0);
    } else {
        setRect(0, 0, CAR_H, CAR_W);   // E/W: wide + short
        setTransformOriginPoint(CAR_H / 2.0, CAR_W / 2.0);
    }
    // Clear any rotation left over from a Bezier turn; the new rect shape
    // already represents the post-turn heading.
    setRotation(0);
    setSceneCenter(c);
    positionBlinkers();   // local-coord positions depend on direction
}

// Look ahead from waypoint `fromIdx` for the next axis-aligned segment and
// return its cardinal direction. Diagonal segments (turn edges in the graph)
// inherit the upcoming straight segment's direction so cars "anticipate" the
// turn instead of pointing the previous way while crossing the intersection.
int CarItem::segmentDirection(int fromIdx) const {
    auto isDiagonal = [](qreal dx, qreal dy) {
        return qAbs(qAbs(dx) - qAbs(dy)) < 1.0 && qAbs(dx) > 0.5;
    };

    for (int i = fromIdx; i + 1 < pathWaypoints.size(); ++i) {
        QPointF a = pathWaypoints[i];
        QPointF b = pathWaypoints[i + 1];
        qreal dx = b.x() - a.x();
        qreal dy = b.y() - a.y();
        if (!isDiagonal(dx, dy)) {
            if (qAbs(dx) > qAbs(dy)) return (dx > 0) ? 1 : 3;   // E or W
            return (dy > 0) ? 2 : 0;                            // S or N
        }
    }
    // Path ends on (or only contains) diagonal segments — fall back to the
    // dominant axis of the immediate segment, or keep current direction.
    if (fromIdx + 1 < pathWaypoints.size()) {
        QPointF a = pathWaypoints[fromIdx];
        QPointF b = pathWaypoints[fromIdx + 1];
        qreal dx = b.x() - a.x();
        qreal dy = b.y() - a.y();
        if (qAbs(dx) >= qAbs(dy)) return (dx > 0) ? 1 : 3;
        return (dy > 0) ? 2 : 0;
    }
    return direction;
}

// Drive one tick along the path. Three modes, checked in priority order:
//
//   1) `turning` true: animate the quadratic Bezier turn. Interpolate the
//      position along (turnP0, turnP1, turnP2) and rotate the sprite
//      smoothly from 0 to ±90°. When progress reaches 1, finishPathTurn()
//      snaps to the post-diagonal waypoint, swaps the rect to the post
//      direction, clears rotation, and advances pathCursor.
//
//   2) stopAtNextWaypoint true: hold at pathStopPos (set by the window
//      each tick — rank-adjusted for queue stacking). Never back up: if
//      the car has already passed its stop point along its direction of
//      travel, just hold.
//
//   3) Otherwise: drive toward pathWaypoints[pathCursor] at speed 3. On
//      arrival, advance the cursor. If the new segment is a graph turn
//      edge (isDiagonalSegment), start a Bezier turn; otherwise reorient
//      the sprite for the next straight segment.
bool CarItem::movePath() {
    if (pathCursor >= pathWaypoints.size()) return true;

    const qreal speed = 3.0;

    // ── Bezier turn ─────────────────────────────────────────────────
    if (turning) {
        turnProgress += turnSpeed;
        if (turnProgress >= 1.0) {
            finishPathTurn();
            return false;
        }
        qreal t = turnProgress;
        qreal u = 1.0 - t;
        QPointF c = u * u * turnP0 + 2.0 * u * t * turnP1 + t * t * turnP2;
        setSceneCenter(c);
        // Rotate from 0 (entering pre-direction's heading) to ±90 (post
        // direction's heading). Qt's setRotation is clockwise-positive.
        setRotation((turnIsRight ? +90.0 : -90.0) * t);
        return false;
    }

    QPointF center = sceneCenter();

    // ── Red-light hold at rank-adjusted stop position ───────────────
    if (stopAtNextWaypoint) {
        bool atOrPast = false;
        switch (direction) {
        case 0: atOrPast = center.y() <= pathStopPos.y(); break;
        case 1: atOrPast = center.x() >= pathStopPos.x(); break;
        case 2: atOrPast = center.y() >= pathStopPos.y(); break;
        case 3: atOrPast = center.x() <= pathStopPos.x(); break;
        }
        if (atOrPast) return false;

        qreal dx = pathStopPos.x() - center.x();
        qreal dy = pathStopPos.y() - center.y();
        qreal dist = qSqrt(dx * dx + dy * dy);
        if (dist <= speed) {
            setSceneCenter(pathStopPos);
        } else {
            setSceneCenter(QPointF(center.x() + dx / dist * speed,
                                   center.y() + dy / dist * speed));
        }
        return false;
    }

    // ── Normal forward drive toward the next waypoint ───────────────
    QPointF target = pathWaypoints[pathCursor];
    qreal dx = target.x() - center.x();
    qreal dy = target.y() - center.y();
    qreal dist = qSqrt(dx * dx + dy * dy);

    if (dist <= speed) {
        setSceneCenter(target);
        pathCursor++;
        if (pathCursor >= pathWaypoints.size()) return true;

        // If the new segment is a turn edge, start the Bezier so the
        // sprite rotates smoothly through the intersection. Otherwise
        // just reorient for the next straight segment.
        if (isDiagonalSegment(pathCursor - 1, pathCursor)) {
            startPathTurn();
            return false;
        }
        int newDir = segmentDirection(pathCursor - 1);
        if (newDir >= 0 && newDir != direction) setOrientation(newDir);
        return false;
    }

    qreal nx = dx / dist;
    qreal ny = dy / dist;
    setSceneCenter(QPointF(center.x() + nx * speed, center.y() + ny * speed));
    return false;
}

// ── Path-mode Bezier turn ────────────────────────────────────────────

bool CarItem::isDiagonalSegment(int fromIdx, int toIdx) const {
    if (fromIdx < 0 || toIdx < 0) return false;
    if (fromIdx >= pathNodeIds.size() || toIdx >= pathNodeIds.size()) return false;
    int intId, dir;
    if (!approachNodeInfo(pathNodeIds[fromIdx], intId, dir)) return false;
    int fc = nodeChain(pathNodeIds[fromIdx]);
    int tc = nodeChain(pathNodeIds[toIdx]);
    return fc >= 0 && tc >= 0 && fc != tc;
}

// Set up a Bezier turn from the car's current position (= pathWaypoints
// [pathCursor - 1], the approach) to pathWaypoints[pathCursor] (the
// post-diagonal node). Control point sits at the corner of the L-shape
// formed by the pre and post tangent directions:
//   vertical → horizontal: P1 = (P0.x, P2.y)
//   horizontal → vertical: P1 = (P2.x, P0.y)
// turnIsRight picks the rotation sign; turnSpeed sizes the progress
// increment to roughly match the normal 3-px/tick driving speed along
// the control-polygon length.
void CarItem::startPathTurn() {
    if (pathCursor < 1 || pathCursor >= pathWaypoints.size()) return;

    turnP0 = pathWaypoints[pathCursor - 1];
    turnP2 = pathWaypoints[pathCursor];

    int preDir  = direction;
    int postDir = segmentDirection(pathCursor);
    if (postDir < 0) postDir = preDir;

    bool preVertical  = (preDir  == 0 || preDir  == 2);
    bool postVertical = (postDir == 0 || postDir == 2);
    if (preVertical && !postVertical) {
        turnP1 = QPointF(turnP0.x(), turnP2.y());
    } else if (!preVertical && postVertical) {
        turnP1 = QPointF(turnP2.x(), turnP0.y());
    } else {
        // Same-axis "turn" shouldn't happen in our graph; fall back to
        // a straight-line midpoint so the Bezier is just the segment.
        turnP1 = (turnP0 + turnP2) / 2.0;
    }

    // Direction change → rotation sign.
    int delta = (postDir - preDir + 4) % 4;
    turnIsRight = (delta == 1);   // CW; delta==3 ⇒ left/CCW

    // Constant-speed traversal: progress increment per tick = 3 / curveLen.
    // Control-polygon length is a fine upper bound for a 90° quadratic.
    qreal d01 = qSqrt(qPow(turnP1.x() - turnP0.x(), 2) +
                      qPow(turnP1.y() - turnP0.y(), 2));
    qreal d12 = qSqrt(qPow(turnP2.x() - turnP1.x(), 2) +
                      qPow(turnP2.y() - turnP1.y(), 2));
    qreal len = d01 + d12;
    turnSpeed = (len > 0.5) ? (3.0 / len) : 0.05;

    turning      = true;
    turnProgress = 0;
}

void CarItem::finishPathTurn() {
    setSceneCenter(turnP2);
    int postDir = segmentDirection(pathCursor);
    if (postDir < 0) postDir = direction;
    setOrientation(postDir);    // swaps rect, resets rotation, repositions blinkers
    turning      = false;
    turnProgress = 0;
    pathCursor++;               // we've arrived at the post-diagonal waypoint
}
