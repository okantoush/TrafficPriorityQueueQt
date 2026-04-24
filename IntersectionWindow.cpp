#include "IntersectionWindow.h"
#include <QPen>
#include <QBrush>
#include <QKeyEvent>
#include <QGraphicsTextItem>
#include <QFont>
#include <QDebug>
#include <algorithm>

// Stop/clear coords must be on the APPROACH side so cars hit stop before clear.
// Intersection box is (225,225)–(375,375). Each road is 150 px wide (75 per
// direction), giving three 25-px lanes per direction — enough room for a
// 16-px-wide car plus padding in each lane with no overlap.
//   North (UP,  y−−): approach from y=580, entry edge y=375  → stop just outside, clear just inside
//   South (DOWN,y++): approach from y=10,  entry edge y=225  → stop just outside, clear just inside
//   East  (RIGHT,x++): approach from x=10, entry edge x=225  → stop just outside, clear just inside
//   West  (LEFT, x−−): approach from x=580,entry edge x=375  → stop just outside, clear just inside
static const qreal STOP_N  = 385, STOP_S  = 215, STOP_E  = 215, STOP_W  = 385;
static const qreal CLEAR_N = 375, CLEAR_S = 225, CLEAR_E = 225, CLEAR_W = 375;

// Right-hand traffic: drivers stay on the right side of the road.
// Each direction has 3 zones in its 75-px road half: turn lane (20 px, next
// to median) + two straight lanes (27.5 px each). Cars drive CENTERED in
// their straight lane under normal conditions. When an emergency vehicle
// arrives, startLaneSplit() shifts them to YIELD_* edge positions to open
// up a corridor; endLaneSplit() restores them.
// Positions below are top-left x (N/S) or y (E/W) for a 16-px-wide car,
// placed at the middle of each lane zone.
//   North (going UP):    east half  (x 300–375)
//       turn zone 300–320, inner lane 320–347 center=326, outer 347–375 center=353
//   South (going DOWN):  west half  (x 225–300)
//       outer 225–252 center=231, inner 252–280 center=258, turn zone 280–300
//   East  (going RIGHT): south half (y 300–375)  — same pattern as N
//   West  (going LEFT):  north half (y 225–300)  — same pattern as S
// Convention: LANE_[NE][0] = inner (near median); LANE_[SW][1] = inner.
static const qreal LANE_N[2] = { 326, 353 };
static const qreal LANE_S[2] = { 231, 258 };
static const qreal LANE_E[2] = { 326, 353 };
static const qreal LANE_W[2] = { 231, 258 };

// Dedicated left-turn lanes — innermost lane, right up against the median.
// Left-turners merge from their normal lane into these before the intersection.
static const qreal TURN_N = 303;   // N left-turn lane x
static const qreal TURN_S = 281;   // S left-turn lane x
static const qreal TURN_E = 303;   // E left-turn lane y
static const qreal TURN_W = 281;   // W left-turn lane y

// ── Constructor ───────────────────────────────────────────────────────────
IntersectionWindow::IntersectionWindow(bool manualMode)
    : m_manualMode(manualMode),
    m_nextIsEmergency(false),
    m_nextIsTurnLeft(false),
    m_carCounter(0),
    m_emergencyWaiting(false),
    m_splittingDir(-1),
    m_splitComplete(false),
    m_releasedEmergency(nullptr),
    m_tickCount(0),
    m_hud(nullptr)
{
    scene = new QGraphicsScene(this);
    setScene(scene);
    setFixedSize(620, 620);
    scene->setSceneRect(0, 0, 600, 600);
    setBackgroundBrush(Qt::white);

    timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, &IntersectionWindow::updateSimulation);

    buildScene();
    timer->start(50);
}

// ── Static scene: roads, lights ───────────────────────────────────────────
void IntersectionWindow::buildScene()
{
    QPen noPen(Qt::NoPen);
    // Roads: 150 px wide each, intersection at (225,225)–(375,375).
    scene->addRect(225, 0,   150, 600, noPen, QBrush(QColor(80,80,80)));
    scene->addRect(0,   225, 600, 150, noPen, QBrush(QColor(80,80,80)));

    QPen dashPen(Qt::white, 1, Qt::DashLine);
    // Median center line
    scene->addLine(300, 0,   300, 225, dashPen);
    scene->addLine(300, 375, 300, 600, dashPen);
    scene->addLine(0,   300, 225, 300, dashPen);
    scene->addLine(375, 300, 600, 300, dashPen);
    // Lane dividers between outer and inner straight lanes in each half.
    // Zones split at x=252 (S outer/inner) and x=348 (N inner/outer).
    scene->addLine(252, 0,   252, 225, dashPen);
    scene->addLine(252, 375, 252, 600, dashPen);
    scene->addLine(348, 0,   348, 225, dashPen);
    scene->addLine(348, 375, 348, 600, dashPen);
    // Horizontal road: divider at y=252 (W outer/inner) and y=348 (E inner/outer).
    scene->addLine(0,   252, 225, 252, dashPen);
    scene->addLine(375, 252, 600, 252, dashPen);
    scene->addLine(0,   348, 225, 348, dashPen);
    scene->addLine(375, 348, 600, 348, dashPen);

    QPen stopPen(Qt::white, 3);
    scene->addLine(300, STOP_N, 375, STOP_N, stopPen);   // N stop line (east half)
    scene->addLine(225, STOP_S, 300, STOP_S, stopPen);   // S stop line (west half)
    scene->addLine(STOP_E, 300, STOP_E, 375, stopPen);   // E stop line (south half)
    scene->addLine(STOP_W, 225, STOP_W, 300, stopPen);   // W stop line (north half)

    // Left-turn lane markings — yellow dashed line between turn lane and
    // inner straight lane, on the approach side only.
    // Turn lane at 303 (N/E) or 281 (S/W); inner straight at 322 (N/E) or
    // 262 (S/W); boundary sits at 320 / 280.
    QPen turnLanePen(QColor(255, 200, 0), 1.5, Qt::DashLine);
    scene->addLine(320, 385, 320, 590, turnLanePen);   // N approach
    scene->addLine(280, 10,  280, 215, turnLanePen);   // S approach
    scene->addLine(10,  320, 215, 320, turnLanePen);   // E approach
    scene->addLine(385, 280, 590, 280, turnLanePen);   // W approach

    // Left-turn arrows on the road (positioned on the approach side of each
    // turn lane, a ways back from the stop line).
    QFont arrowFont("Helvetica", 18, QFont::Bold);
    auto addArrow = [&](qreal x, qreal y, const QString& s) {
        QGraphicsTextItem* a = scene->addText(s, arrowFont);
        a->setDefaultTextColor(QColor(255, 220, 0));
        a->setPos(x, y);
    };
    addArrow(TURN_N - 5, 430, "↰");      // N
    addArrow(TURN_S - 5, 150, "↲");      // S
    addArrow(130, TURN_E - 14, "↱");     // E
    addArrow(450, TURN_W - 14, "↵");     // W

    // Straight-through lights — at the far corners, just outside the
    // intersection, where drivers see them across the box.
    lightIndicators[0] = new DirectionalLight(0, QPointF(377, 201));   // N: NE corner
    lightIndicators[1] = new DirectionalLight(1, QPointF(377, 377));   // E: SE corner
    lightIndicators[2] = new DirectionalLight(2, QPointF(201, 377));   // S: SW corner
    lightIndicators[3] = new DirectionalLight(3, QPointF(201, 201));   // W: NW corner
    for (int i = 0; i < 4; i++) scene->addItem(lightIndicators[i]);

    // Left-turn lights — on the inner side, just beside each turn lane.
    turnLightIndicators[0] = new DirectionalLight(0, QPointF(295, 201));  // N
    turnLightIndicators[1] = new DirectionalLight(1, QPointF(377, 295));  // E
    turnLightIndicators[2] = new DirectionalLight(2, QPointF(283, 377));  // S
    turnLightIndicators[3] = new DirectionalLight(3, QPointF(201, 283));  // W
    for (int i = 0; i < 4; i++) scene->addItem(turnLightIndicators[i]);

    if (!m_manualMode) buildSimulationCars();

    // HUD (both modes — shows legend, spawn modes, and controls)
    QGraphicsRectItem* hudBg = scene->addRect(2, 2, 320, m_manualMode ? 90 : 76,
                                              QPen(Qt::NoPen), QBrush(QColor(0,0,0,180)));
    hudBg->setZValue(29);

    m_hud = scene->addText("");
    m_hud->setDefaultTextColor(Qt::white);
    m_hud->setFont(QFont("Helvetica", 9));
    m_hud->setPos(6, 4);
    m_hud->setZValue(30);

    updateHud();
    updateLightVisuals();
}

// ── Simulation mode: no initial cars — everything spawns on schedule ──────
void IntersectionWindow::buildSimulationCars()
{
    // All spawns are handled by processSpawnSchedule() based on tick count
}

// ── Scripted spawn schedule (simulation mode only) ───────────────────────
// Tells a story: basic traffic → builds up → emergency vehicle demo
void IntersectionWindow::processSpawnSchedule()
{
    // At 50ms/tick: tick 20 = 1s, 100 = 5s, 200 = 10s, etc.

    // ═══════════════════════════════════════════════════════════════════
    // ACT 1: Basic intersection — a couple of cars from N and S
    // ═══════════════════════════════════════════════════════════════════
    if (m_tickCount == 1) {
        spawnCar(0, false);   // North
        spawnCar(2, false);   // South
    }
    if (m_tickCount == 30) {
        spawnCar(0, false);   // North — 2nd car
        spawnCar(2, false);   // South — 2nd car
    }

    // ═══════════════════════════════════════════════════════════════════
    // ACT 2: East/West traffic arrives — triggers phase switch + yellow
    // ═══════════════════════════════════════════════════════════════════
    if (m_tickCount == 140) {
        spawnCar(1, false);   // East
        spawnCar(3, false);   // West
    }
    if (m_tickCount == 170) {
        spawnCar(1, false);   // East — 2nd
        spawnCar(3, false);   // West — 2nd
    }

    // ═══════════════════════════════════════════════════════════════════
    // ACT 3: Traffic builds up — queuing visible on all directions
    //         (left-turners sprinkled in to show the turn lane + blinker)
    // ═══════════════════════════════════════════════════════════════════
    if (m_tickCount == 300) {
        spawnCar(0, false);
        spawnCar(0, false, true);   // ↰ N left-turner
        spawnCar(2, false);
        spawnCar(2, false, true);   // ↰ S left-turner
    }
    if (m_tickCount == 340) {
        spawnCar(1, false);
        spawnCar(1, false, true);   // ↰ E left-turner
        spawnCar(3, false);
        spawnCar(3, false, true);   // ↰ W left-turner
    }
    if (m_tickCount == 400) {
        spawnCar(0, false);
        spawnCar(2, false);
        spawnCar(1, false);
        spawnCar(3, false);
    }

    // ═══════════════════════════════════════════════════════════════════
    // ACT 4: First emergency vehicle — lane split from North
    // ═══════════════════════════════════════════════════════════════════
    if (m_tickCount == 520) {
        // Build up North queue so the split is visible
        spawnCar(0, false);
        spawnCar(0, false);
        spawnCar(0, false);
    }
    if (m_tickCount == 600) {
        spawnCar(0, true);    // 🚨 Emergency from North!
    }

    // ═══════════════════════════════════════════════════════════════════
    // ACT 5: Traffic resumes, then another emergency from East
    // ═══════════════════════════════════════════════════════════════════
    if (m_tickCount == 780) {
        spawnCar(1, false);
        spawnCar(1, false);
        spawnCar(1, false);
        spawnCar(3, false);
        spawnCar(3, false);
    }
    if (m_tickCount == 860) {
        spawnCar(1, true);    // 🚨 Emergency from East!
    }

    // ═══════════════════════════════════════════════════════════════════
    // ACT 6: Steady traffic continues flowing
    // ═══════════════════════════════════════════════════════════════════
    if (m_tickCount == 1000) {
        spawnCar(0, false);
        spawnCar(2, false);
        spawnCar(1, false);
        spawnCar(3, false);
    }
    if (m_tickCount == 1100) {
        spawnCar(0, false);
        spawnCar(0, false);
        spawnCar(2, false);
        spawnCar(2, false);
    }
    if (m_tickCount == 1200) {
        spawnCar(1, false);
        spawnCar(1, false);
        spawnCar(3, false);
        spawnCar(3, false);
    }

    // ═══════════════════════════════════════════════════════════════════
    // ACT 7: Double emergency — South while traffic is heavy
    // ═══════════════════════════════════════════════════════════════════
    if (m_tickCount == 1350) {
        spawnCar(2, false);
        spawnCar(2, false);
        spawnCar(2, false);
        spawnCar(0, false);
        spawnCar(0, false);
    }
    if (m_tickCount == 1440) {
        spawnCar(2, true);    // 🚨 Emergency from South!
    }

    // ═══════════════════════════════════════════════════════════════════
    // ACT 8: Final wave — ongoing traffic to keep the sim alive
    // ═══════════════════════════════════════════════════════════════════
    // Periodic spawns every ~200 ticks after act 7
    if (m_tickCount > 1600 && m_tickCount % 100 == 0) {
        int dir = (m_tickCount / 100) % 4;
        spawnCar(dir, false);
    }
    if (m_tickCount > 1600 && m_tickCount % 150 == 0) {
        int dir = ((m_tickCount / 150) + 1) % 4;
        spawnCar(dir, false);
    }
}

// ── Emergency corridor + yield positions ────────────────────────────────
// Normally cars sit at the LANE_* centers. When an emergency arrives,
// startLaneSplit() shifts them to these YIELD_* edge positions to carve
// out a center corridor the emergency drives through.
//   N/E yielded: inner → 320 (hugs turn-lane edge), outer → 359 (hugs curb).
//                Corridor: 336..359 = 23 px wide. Emergency at 340.
//   S/W yielded: outer → 225 (hugs curb), inner → 264 (hugs turn-lane edge).
//                Corridor: 241..264 = 23 px wide. Emergency at 244.
static const qreal EMG_CENTER_N = 340;
static const qreal EMG_CENTER_S = 244;
static const qreal EMG_CENTER_E = 340;
static const qreal EMG_CENTER_W = 244;

static const qreal YIELD_N[2] = { 320, 359 };   // [inner, outer]
static const qreal YIELD_E[2] = { 320, 359 };
static const qreal YIELD_S[2] = { 225, 264 };   // [outer, inner]
static const qreal YIELD_W[2] = { 225, 264 };

// ── Core spawn logic ──────────────────────────────────────────────────────
void IntersectionWindow::spawnCarAt(int dir, qreal spawnX, qreal spawnY, bool emergency, bool turnLeft)
{
    // Assign to the lane with fewer cars (fills lanes side by side)
    // Left-turners don't count toward the straight-lane totals.
    int count0 = 0, count1 = 0;
    for (CarItem* c : cars) {
        if (c->direction == dir && !c->data->isEmergency && !c->data->willTurnLeft) {
            if (c->laneIndex == 0) count0++;
            else                   count1++;
        }
    }
    int lane = (count0 <= count1) ? 0 : 1;

    qreal stop, clear;
    switch (dir) {
    case 0: stop=STOP_N; clear=CLEAR_N; break;
    case 1: stop=STOP_E; clear=CLEAR_E; break;
    case 2: stop=STOP_S; clear=CLEAR_S; break;
    default:stop=STOP_W; clear=CLEAR_W; break;
    }

    // Set lateral position based on car type
    bool vertical = (dir == 0 || dir == 2);
    qreal finalLateral;   // where the car will end up (drives at this lateral)
    qreal spawnLateral;   // where it first appears (may merge from normal lane to turn lane)

    if (emergency) {
        // Emergency cars spawn at the center of the two lanes
        qreal center;
        switch (dir) {
        case 0: center = EMG_CENTER_N; break;
        case 1: center = EMG_CENTER_E; break;
        case 2: center = EMG_CENTER_S; break;
        default:center = EMG_CENTER_W; break;
        }
        finalLateral = center;
        spawnLateral = center;
    } else if (turnLeft) {
        // Left-turners: spawn in a normal lane (inner, lane 0) then merge to turn lane
        qreal normalLateral, turnLateral;
        switch (dir) {
        case 0: normalLateral = LANE_N[0]; turnLateral = TURN_N; break;
        case 1: normalLateral = LANE_E[0]; turnLateral = TURN_E; break;
        case 2: normalLateral = LANE_S[1]; turnLateral = TURN_S; break;   // inner S lane is [1]
        default:normalLateral = LANE_W[1]; turnLateral = TURN_W; break;   // inner W lane is [1]
        }
        spawnLateral = normalLateral;
        finalLateral = turnLateral;    // merges to this over time via animateLateral
    } else {
        // Normal cars: set lateral position to match chosen lane
        qreal laneLateral;
        switch (dir) {
        case 0: laneLateral = LANE_N[lane]; break;
        case 1: laneLateral = LANE_E[lane]; break;
        case 2: laneLateral = LANE_S[lane]; break;
        default:laneLateral = LANE_W[lane]; break;
        }
        spawnLateral = laneLateral;
        finalLateral = laneLateral;
    }

    if (vertical) spawnX = spawnLateral;
    else          spawnY = spawnLateral;

    // ── Anti-overlap: if a previously-spawned car is still near the scene
    //    entry in the SAME direction and SAME lateral position, push this
    //    spawn farther back so they don't stack on top of each other.
    //    Happens most visibly in manual mode when direction keys are mashed.
    static const qreal SPAWN_GAP = 1.5;    // tighter follow-distance at spawn
    static const qreal CAR_LEN   = 28.0;   // matches CAR_H in CarItem.cpp
    qreal ourLateral = vertical ? spawnX : spawnY;
    for (CarItem* c : cars) {
        if (c->direction != dir) continue;
        qreal cLat = vertical ? c->x() : c->y();
        if (qAbs(cLat - ourLateral) > 10.0) continue;    // different lane
        switch (dir) {
        case 0: spawnY = qMax(spawnY, c->y() + CAR_LEN + SPAWN_GAP); break;
        case 1: spawnX = qMin(spawnX, c->x() - CAR_LEN - SPAWN_GAP); break;
        case 2: spawnY = qMin(spawnY, c->y() - CAR_LEN - SPAWN_GAP); break;
        case 3: spawnX = qMax(spawnX, c->x() + CAR_LEN + SPAWN_GAP); break;
        }
    }

    QString prefix = emergency ? "EMG" : (turnLeft ? "L" : "Car");
    QString id = prefix + QString::number(m_carCounter++);
    Node*    n   = new Node(id, emergency, turnLeft);
    CarItem* car = new CarItem(n, dir, lane);
    car->setPos(spawnX, spawnY);
    car->stopCoord     = stop;
    car->effectiveStop = stop;
    car->clearCoord    = clear;

    // Lateral tracking — straight cars stay at lane lateral; turn cars merge to turn lateral
    car->lateralTarget   = finalLateral;
    car->originalLateral = finalLateral;

    scene->addItem(car);
    cars.append(car);

    // Left-turners are NOT queued in the controller's straight-lanes.
    // Their release is handled directly in updateSimulation based on the turn light.
    if (!turnLeft) controller.addCar(dir, n);

    // If emergency, immediately start lane splitting
    if (emergency) {
        startLaneSplit(dir);
    }

    QString dirs[4] = {"North","East","South","West"};
    qDebug() << (emergency ? "🚨" : (turnLeft ? "↰" : "🚗")) << id << "from" << dirs[dir];
}

// ── Spawn one car in a direction (lane chosen by spawnCarAt) ─────────────
void IntersectionWindow::spawnCar(int dir, bool emergency, bool turnLeft)
{
    // Pass a placeholder lateral — spawnCarAt will override based on car type
    qreal spawnX, spawnY;
    switch (dir) {
    case 0: spawnX=LANE_N[0]; spawnY=580; break;   // lateral overridden
    case 1: spawnX=10;        spawnY=LANE_E[0]; break;
    case 2: spawnX=LANE_S[0]; spawnY=10;  break;
    default:spawnX=580;       spawnY=LANE_W[0]; break;
    }
    spawnCarAt(dir, spawnX, spawnY, emergency, turnLeft);
    m_nextIsEmergency = false;
    updateHud();
}

// Turn light for direction `dir` is controlled by the dedicated protected
// left-turn phase in TrafficController. Turn-phase duration scales with the
// queue of waiting left-turn cars (pushed to controller each tick below).
bool IntersectionWindow::isTurnLightGreen(int dir) const
{
    return controller.getTurnLightState(dir) == GREEN;
}

// ── HUD ───────────────────────────────────────────────────────────────────
void IntersectionWindow::updateHud()
{
    if (!m_hud) return;

    QString emergencyLine = m_nextIsEmergency
                                ? "<font color='#ff4444'>🚨 next spawn = EMERGENCY</font><br>"
                                : "";
    QString turnLine = m_nextIsTurnLeft
                           ? "<font color='#ffcc00'>↰ next spawn = LEFT TURN</font><br>"
                           : "";

    QString controls = m_manualMode
                           ? "<font color='#aaaaaa'>N/E/S/W = car &nbsp;|&nbsp; V = arm emergency &nbsp;|&nbsp; L = arm left-turn &nbsp;|&nbsp; R = restart</font>"
                           : "<font color='#aaaaaa'>V or L then N/E/S/W = inject &nbsp;|&nbsp; R = restart</font>";

    QString legend =
        "<font color='#8fd9ff'>■</font> car &nbsp;"
        "<font color='#ff6060'>■</font> emergency &nbsp;"
        "<font color='#1e90ff'>■</font> left-turner "
        "<font color='#ffcc00'>(blinker)</font><br>";

    QString carCount = m_manualMode
                           ? "<font color='#cccccc'>Cars on road: " + QString::number(cars.size()) + "</font><br>"
                           : "";

    m_hud->setHtml(carCount + legend + emergencyLine + turnLine + controls);
}

// ── Clear ─────────────────────────────────────────────────────────────────
void IntersectionWindow::clearScene()
{
    for (CarItem* car : cars) { scene->removeItem(car); delete car->data; delete car; }
    cars.clear();
    scene->clear();
    controller        = TrafficController();
    m_nextIsEmergency = false;
    m_nextIsTurnLeft  = false;
    m_emergencyWaiting = false;
    m_splittingDir    = -1;
    m_splitComplete   = false;
    m_releasedEmergency = nullptr;
    m_tickCount       = 0;
    m_carCounter      = 0;
    m_hud             = nullptr;
}

void IntersectionWindow::restartSimulation()
{
    timer->stop();
    clearScene();
    buildScene();
    timer->start(50);
    qDebug() << "🔄 Restarted";
}

// ── Key press ─────────────────────────────────────────────────────────────
void IntersectionWindow::keyPressEvent(QKeyEvent* event)
{
    switch (event->key()) {

    case Qt::Key_R:
        restartSimulation();
        break;

    // V arms emergency mode — next direction key spawns an emergency vehicle.
    // Works in BOTH manual and simulation mode.
    case Qt::Key_V:
        m_nextIsEmergency = !m_nextIsEmergency;
        if (m_nextIsEmergency) m_nextIsTurnLeft = false;
        updateHud();
        qDebug() << (m_nextIsEmergency ? "🚨 Emergency armed — press N/E/S/W"
                                       : "🚨 Emergency disarmed");
        break;

    // L arms left-turn mode — next direction key spawns a left-turning car.
    case Qt::Key_L:
        m_nextIsTurnLeft = !m_nextIsTurnLeft;
        if (m_nextIsTurnLeft) m_nextIsEmergency = false;
        updateHud();
        qDebug() << (m_nextIsTurnLeft ? "↰ Left-turn armed — press N/E/S/W"
                                      : "↰ Left-turn disarmed");
        break;

    // Direction keys — spawn car (manual), emergency (V), or left-turner (L)
    case Qt::Key_N:
        if (m_manualMode || m_nextIsEmergency || m_nextIsTurnLeft)
            spawnCar(0, m_nextIsEmergency, m_nextIsTurnLeft);
        m_nextIsTurnLeft = false;
        break;
    case Qt::Key_E:
        if (m_manualMode || m_nextIsEmergency || m_nextIsTurnLeft)
            spawnCar(1, m_nextIsEmergency, m_nextIsTurnLeft);
        m_nextIsTurnLeft = false;
        break;
    case Qt::Key_S:
        if (m_manualMode || m_nextIsEmergency || m_nextIsTurnLeft)
            spawnCar(2, m_nextIsEmergency, m_nextIsTurnLeft);
        m_nextIsTurnLeft = false;
        break;
    case Qt::Key_W:
        if (m_manualMode || m_nextIsEmergency || m_nextIsTurnLeft)
            spawnCar(3, m_nextIsEmergency, m_nextIsTurnLeft);
        m_nextIsTurnLeft = false;
        break;

    default:
        QGraphicsView::keyPressEvent(event);
    }
}

// ── Light visuals ─────────────────────────────────────────────────────────
void IntersectionWindow::updateLightVisuals()
{
    for (int i = 0; i < 4; i++) {
        LightState st = controller.getLightState(i);
        if (st == GREEN)
            lightIndicators[i]->setColor(Qt::green);
        else if (st == YELLOW)
            lightIndicators[i]->setColor(Qt::yellow);
        else
            lightIndicators[i]->setColor(Qt::red);
    }

    // Turn light reflects the controller's dedicated protected-turn phase state.
    // (Green only during the N+S turn or E+W turn phase — not while the
    //  destination's straight light is green.)
    for (int i = 0; i < 4; i++) {
        LightState st = controller.getTurnLightState(i);
        if (st == GREEN)
            turnLightIndicators[i]->setColor(Qt::green);
        else if (st == YELLOW)
            turnLightIndicators[i]->setColor(Qt::yellow);
        else
            turnLightIndicators[i]->setColor(Qt::red);
    }
}

// ── Lane splitting for emergency vehicles ────────────────────────────────
// Under normal conditions cars drive centered in their lane. When an
// emergency arrives, shift every non-released same-direction car outward
// (toward median or curb depending on which lane they're in) so a corridor
// opens up in the middle for the emergency to drive through. Restore them
// once the emergency has cleared the scene.
void IntersectionWindow::startLaneSplit(int dir)
{
    m_splittingDir  = dir;
    m_splitComplete = false;

    const qreal* yieldPos;
    switch (dir) {
    case 0: yieldPos = YIELD_N; break;
    case 1: yieldPos = YIELD_E; break;
    case 2: yieldPos = YIELD_S; break;
    default:yieldPos = YIELD_W; break;
    }

    for (CarItem* car : cars) {
        if (car->direction != dir) continue;
        if (car->data->isEmergency) continue;   // don't shift the emergency itself
        if (car->inIntersection)    continue;   // already through
        if (car->data->willTurnLeft) continue;  // turners use the turn lane — unaffected

        car->yielding      = true;
        car->lateralTarget = yieldPos[car->laneIndex];
    }

    qDebug() << "🚨 Lane split started for direction" << dir;
}

void IntersectionWindow::endLaneSplit(int dir)
{
    for (CarItem* car : cars) {
        if (car->direction != dir) continue;
        if (!car->yielding) continue;
        car->yielding      = false;
        car->lateralTarget = car->originalLateral;
    }

    m_splittingDir  = -1;
    m_splitComplete = false;
    qDebug() << "🚨 Lane split ended for direction" << dir;
}

bool IntersectionWindow::isLaneSplitComplete(int dir) const
{
    for (CarItem* car : cars) {
        if (car->direction != dir) continue;
        if (!car->yielding) continue;

        bool vertical = (dir == 0 || dir == 2);
        qreal current = vertical ? car->x() : car->y();
        if (qAbs(current - car->lateralTarget) > 1.0)
            return false;
    }
    return true;
}

void IntersectionWindow::animateAllLateral()
{
    for (CarItem* car : cars) {
        car->animateLateral();
    }
}

// ── Compute effective stop positions so cars queue with gaps ──────────────
void IntersectionWindow::computeEffectiveStops()
{
    static const qreal GAP = 4.0;
    static const qreal CAR_LEN = 28.0; // matches CAR_H in CarItem.cpp

    // Group non-released, non-inIntersection cars by (direction, isTurnLane).
    // Emergency cars bypass queueing entirely (they drive through the center).
    // Turn-lane cars queue separately from straight-through cars (different lane).
    // Indices: d*2 + 0 = straight cars for dir d; d*2 + 1 = turn cars for dir d
    QList<CarItem*> byGroup[8];
    for (CarItem* car : cars) {
        if (car->released || car->inIntersection) continue;
        if (car->data->isEmergency) continue;
        int g = car->direction * 2 + (car->data->willTurnLeft ? 1 : 0);
        byGroup[g].append(car);
    }

    for (int g = 0; g < 8; g++) {
        int d = g / 2;
        QList<CarItem*>& list = byGroup[g];
        if (list.isEmpty()) continue;

        // Sort by distance to stop line (closest first)
        if (d == 0) { // North: travels up, closer = smaller y
            std::sort(list.begin(), list.end(), [](CarItem* a, CarItem* b) {
                return a->y() < b->y();
            });
        } else if (d == 1) { // East: travels right, closer = larger x
            std::sort(list.begin(), list.end(), [](CarItem* a, CarItem* b) {
                return a->x() > b->x();
            });
        } else if (d == 2) { // South: travels down, closer = larger y
            std::sort(list.begin(), list.end(), [](CarItem* a, CarItem* b) {
                return a->y() > b->y();
            });
        } else { // West: travels left, closer = smaller x
            std::sort(list.begin(), list.end(), [](CarItem* a, CarItem* b) {
                return a->x() < b->x();
            });
        }

        // First car gets the original stop coord
        list[0]->effectiveStop = list[0]->stopCoord;

        // Subsequent cars stop behind the one ahead with a gap
        for (int i = 1; i < list.size(); i++) {
            qreal aheadStop = list[i-1]->effectiveStop;
            qreal behindStop;

            if (d == 0) { // North: behind = further down = larger y
                behindStop = aheadStop + CAR_LEN + GAP;
            } else if (d == 1) { // East: behind = further left = smaller x, stop is leading edge
                behindStop = aheadStop - CAR_LEN - GAP;
            } else if (d == 2) { // South: behind = further up = smaller y, stop is leading edge
                behindStop = aheadStop - CAR_LEN - GAP;
            } else { // West: behind = further right = larger x
                behindStop = aheadStop + CAR_LEN + GAP;
            }

            // Don't let effective stop be "better" than original (closer to intersection)
            if (d == 0) // North: stop is min y, behind = max
                list[i]->effectiveStop = qMax(behindStop, list[i]->stopCoord);
            else if (d == 1) // East: stop is max x, behind = min
                list[i]->effectiveStop = qMin(behindStop, list[i]->stopCoord);
            else if (d == 2) // South: stop is max y, behind = min
                list[i]->effectiveStop = qMin(behindStop, list[i]->stopCoord);
            else // West: stop is min x, behind = max
                list[i]->effectiveStop = qMax(behindStop, list[i]->stopCoord);
        }
    }

    // Released / inIntersection cars: just keep effectiveStop = stopCoord (unused)
    for (CarItem* car : cars) {
        if (car->released || car->inIntersection)
            car->effectiveStop = car->stopCoord;
    }
}

// ── Check if any car is currently inside the intersection box ─────────────
bool IntersectionWindow::isIntersectionClear() const
{
    for (CarItem* car : cars) {
        if (car->inIntersection) return false;
    }
    return true;
}

// ── Per-tick update ───────────────────────────────────────────────────────
void IntersectionWindow::updateSimulation()
{
    // ── Scripted spawns in simulation mode ───────────────────────────
    if (!m_manualMode) {
        processSpawnSchedule();
        m_tickCount++;
    }

    // ── Update congestion statistics in traffic controller ───────────
    controller.updateCongestionStats();

    // ── Tell controller how many left-turners are waiting per direction.
    //    Controller uses this to:
    //      (a) decide whether to even run a protected turn phase
    //          (nextPhase skips empty phases), and
    //      (b) scale that phase's duration with the queue size
    //          (more turners → longer green).
    int turnCounts[4] = { 0, 0, 0, 0 };
    for (CarItem* car : cars) {
        if (!car->data->willTurnLeft) continue;
        if (car->released || car->inIntersection || car->turnCompleted) continue;
        if (car->direction < 0 || car->direction >= 4) continue;
        turnCounts[car->direction]++;
    }
    for (int d = 0; d < 4; d++)
        controller.setTurnQueueSize(d, turnCounts[d]);

    // ── Animate lateral shifts every tick (lane splitting + turn merge) ──
    animateAllLateral();

    // ── Flash blinkers on left-turn cars ──────────────────────────────
    for (CarItem* car : cars) car->updateBlinker();

    // ── Compute effective stop positions (anti-stacking) ─────────────
    computeEffectiveStops();

    // ── Emergency processing ──────────────────────────────────────────
    if (controller.hasEmergency()) {
        m_emergencyWaiting = true;

        // Wait for lane split to complete, then release immediately.
        // Emergency car keeps driving forward (never stops at stop line).
        // All other lights are RED so no new cars enter the intersection.
        if (m_splittingDir >= 0 && !m_splitComplete) {
            if (isLaneSplitComplete(m_splittingDir)) {
                m_splitComplete = true;
                qDebug() << "🚨 Lane split complete — releasing emergency";
            }
        }

        // Release as soon as split is done (or immediately if no split needed)
        if (m_splitComplete || m_splittingDir < 0) {
            Node* front = controller.peekEmergency();
            for (CarItem* car : cars) {
                if (car->data == front && !car->released) {
                    controller.releaseEmergency(car->direction);
                    car->released   = true;
                    car->atStopLine = false;
                    m_releasedEmergency = car;
                    qDebug() << "🚑 Emergency car released — bypassing traffic";
                    break;
                }
            }
        }

        updateLightVisuals();

        QList<CarItem*> toRemove;
        for (CarItem* car : cars) {
            if (car->moveForward()) toRemove.append(car);
        }
        for (CarItem* car : toRemove) {
            if (car == m_releasedEmergency) m_releasedEmergency = nullptr;
            cars.removeOne(car);
            scene->removeItem(car);
            delete car->data;
            delete car;
        }
        if (m_manualMode) updateHud();
        return;
    }

    // ── Released emergency car still on screen — keep split open ─────
    if (m_releasedEmergency != nullptr) {
        // Move all cars, check if the released emergency exits
        updateLightVisuals();

        QList<CarItem*> toRemove;
        for (CarItem* car : cars) {
            if (car->moveForward()) toRemove.append(car);
        }
        for (CarItem* car : toRemove) {
            if (car == m_releasedEmergency) {
                m_releasedEmergency = nullptr;
                // Emergency has left — end lane split
                if (m_splittingDir >= 0) endLaneSplit(m_splittingDir);
            }
            cars.removeOne(car);
            scene->removeItem(car);
            delete car->data;
            delete car;
        }
        if (m_manualMode) updateHud();

        // If emergency just left, fall through to normal flow next tick
        if (m_releasedEmergency != nullptr) return;
    }

    m_emergencyWaiting = false;

    // ── Normal flow ───────────────────────────────────────────────────
    controller.advanceLights();

    // Release straight cars when their own light is green
    for (CarItem* car : cars) {
        if (car->data->willTurnLeft) continue;  // turn cars handled separately below
        if (!car->released && car->atStopLine) {
            Node* ok = controller.tryRelease(car->direction);
            if (ok) { car->released = true; car->atStopLine = false; }
        }
    }

    // Release left-turn cars when their destination direction's light is green.
    // Turn cars are not in controller queues — handled directly here.
    for (CarItem* car : cars) {
        if (!car->data->willTurnLeft) continue;
        if (car->released || !car->atStopLine) continue;
        if (isTurnLightGreen(car->direction)) {
            car->released   = true;
            car->atStopLine = false;
            qDebug() << "↰ Turn released:" << car->data->vehicleID
                     << "from dir" << car->direction
                     << "→ merging into dir" << ((car->direction + 3) % 4);
        }
    }

    updateLightVisuals();
    if (m_manualMode) updateHud();

    QList<CarItem*> toRemove;
    for (CarItem* car : cars) {
        if (car->moveForward()) toRemove.append(car);
    }
    for (CarItem* car : toRemove) {
        cars.removeOne(car);
        scene->removeItem(car);
        delete car->data;
        delete car;
    }
}
