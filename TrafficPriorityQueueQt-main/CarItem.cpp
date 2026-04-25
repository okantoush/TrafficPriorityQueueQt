#include "CarItem.h"
#include <QBrush>
#include <QPen>

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

    // Left-turn state
    willTurnLeft   = node->willTurnLeft;
    turning        = false;
    turnCompleted  = false;
    turnProgress   = 0.0;
    blinkTick      = 0;
    blinker        = nullptr;
    mergeDelayTicks    = 0;
    mergeTargetLateral = 0;
    // Left turn: N→W, E→N, S→E, W→S  ==  (dir + 3) % 4
    destDirection  = willTurnLeft ? (dir + 3) % 4 : dir;


    QPixmap carPixmap;
    QStringList cars = {//list of car images
        ":/images/car1.png",
        ":/images/car2.png",
        ":/images/car3.png"
    };
    QStringList emergency = { //list of emergency vehicle images
        ":/images/firetruck.png",
        ":/images/ambulance.png",
        ":/images/policecar.png"
    };

    if (node->isEmergency) {
        carPixmap.load(emergency[rand() % emergency.size()]);//Load random emergency vehicle image
    } else {
        carPixmap.load(cars[rand() % cars.size()]);// Load random car image
    }
    if (carPixmap.isNull()) //To check for errors when loading the images
        qDebug() << "FAILED TO LOAD CAR IMAGE";


    carPixmap = carPixmap.scaled(
        CAR_W,
        CAR_H,
        Qt::KeepAspectRatio,
        Qt::SmoothTransformation
        );
    setPixmap(carPixmap);
    setTransformOriginPoint(boundingRect().center());
}

//    // Create the blinker as a child item on the LEFT side of the car
//    if (willTurnLeft) {
//        blinker = new QGraphicsRectItem(this);
//        blinker->setBrush(QColor(255, 200, 0));
//        blinker->setPen(QPen(QColor(160, 110, 0), 0.5));
//        // Position on the "front-left" of the car based on direction of travel
//        switch (dir) {
//        case 0:  // N (going up): front = top, left = west edge of rect
//            blinker->setRect(-BLINK - 1, 2, BLINK, BLINK);
//            break;
//        case 1:  // E (going right): front = right, left = top edge of rect
//            blinker->setRect(CAR_H - BLINK - 2, -BLINK - 1, BLINK, BLINK);
//            break;
//        case 2:  // S (going down): front = bottom, left = east edge of rect
//            blinker->setRect(CAR_W + 1, CAR_H - BLINK - 2, BLINK, BLINK);
//            break;
//        case 3:  // W (going left): front = left, left = bottom edge of rect
//            blinker->setRect(2, CAR_W + 1, BLINK, BLINK);
//            break;
//        }
//    }
//}

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
//    bool vertical = (direction == 0 || direction == 2);
//    qreal cx_off = vertical ? CAR_W / 2.0 : CAR_H / 2.0;
//    qreal cy_off = vertical ? CAR_H / 2.0 : CAR_W / 2.0;
//    turnP0 = QPointF(x() + cx_off, y() + cy_off);

    QPointF o = transformOriginPoint();
    turnP0 = QPointF(x() + o.x(), y() + o.y());

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
    switch (direction) {
    case 0: // N → W:  enters heading up, exits heading left
        turnP1 = QPointF(turnP0.x(), 289);
        turnP2 = QPointF(190,        289);
        break;
    case 1: // E → N
        turnP1 = QPointF(311, turnP0.y());
        turnP2 = QPointF(311, 190);
        break;
    case 2: // S → E
        turnP1 = QPointF(turnP0.x(), 311);
        turnP2 = QPointF(410,        311);
        break;
    case 3: // W → S
        turnP1 = QPointF(289, turnP0.y());
        turnP2 = QPointF(289, 410);
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

//    bool vertical = (direction == 0 || direction == 2);
//    if (vertical) {
//        setRect(0, 0, CAR_W, CAR_H);
//        setTransformOriginPoint(CAR_W / 2.0, CAR_H / 2.0);
//    } else {
//        setRect(0, 0, CAR_H, CAR_W);
//        setTransformOriginPoint(CAR_H / 2.0, CAR_W / 2.0);
//    }
    setRotation(0);
    setTransformOriginPoint(boundingRect().center());

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
    case 0: mergeTargetLateral = 326; break;
    case 1: mergeTargetLateral = 326; break;
    case 2: mergeTargetLateral = 258; break;
    case 3: mergeTargetLateral = 258; break;
    }
    mergeDelayTicks = 35;   // ~1.75s at 50ms/tick: visible "drive in turn lane"

    // Remove the blinker — turn is done.
    if (blinker) {
        delete blinker;
        blinker = nullptr;
    }
}

bool CarItem::moveForward() {
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
        return (y() + CAR_H) < 0;
    }

    case 1: { // East — travels RIGHT — x increases — leading edge = right = x() + CAR_H
        qreal nextX     = x() + speed;
        qreal leadingX  = x() + CAR_W;

        if (!inIntersection && leadingX >= clearCoord) {
            inIntersection = true;
            if (willTurnLeft && !turnCompleted) {
                startTurn();
                return false;
            }
        }

        if (!released && !inIntersection && !data->isEmergency) {
            if ((nextX + CAR_W) >= effectiveStop) {
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

        if (!inIntersection && leadingY >= clearCoord) {
            inIntersection = true;
            if (willTurnLeft && !turnCompleted) {
                startTurn();
                return false;
            }
        }

        if (!released && !inIntersection && !data->isEmergency) {
            if ((nextY + CAR_H) >= effectiveStop) {
                setPos(x(), effectiveStop - CAR_W);
                atStopLine = true;
                return false;
            }
        }

        setPos(x(), nextY);
        return y() > 640;
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
        return (x() + CAR_W) < 0;
    }

    }
    return false;
}

void CarItem::animateLateral() {
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

// Blink the turn indicator every ~10 ticks (500ms at 50ms/tick)
void CarItem::updateBlinker() {
    if (!blinker) return;
    blinkTick++;
    bool on = (blinkTick / 10) % 2 == 0;
    blinker->setVisible(on);
}
