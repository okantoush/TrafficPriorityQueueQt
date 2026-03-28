#include "IntersectionWindow.h"
#include <QPen>
#include <QBrush>
#include <QKeyEvent>
#include <QGraphicsTextItem>
#include <QFont>
#include <QDebug>
#include <algorithm>

// Stop/clear coords must be on the APPROACH side so cars hit stop before clear.
// Intersection box is (250,250)–(350,350).
//   North (UP,  y−−): approach from y=580, entry edge y=350  → stop just outside, clear just inside
//   South (DOWN,y++): approach from y=10,  entry edge y=250  → stop just outside, clear just inside
//   East  (RIGHT,x++): approach from x=10, entry edge x=250  → stop just outside, clear just inside
//   West  (LEFT, x−−): approach from x=580,entry edge x=350  → stop just outside, clear just inside
static const qreal STOP_N  = 360, STOP_S  = 240, STOP_E  = 220, STOP_W  = 380;
static const qreal CLEAR_N = 350, CLEAR_S = 250, CLEAR_E = 250, CLEAR_W = 350;

static const qreal LANE_N[2] = { 320, 335 };
static const qreal LANE_S[2] = { 260, 275 };
static const qreal LANE_E[2] = { 260, 275 };
static const qreal LANE_W[2] = { 320, 335 };

// ── Constructor ───────────────────────────────────────────────────────────
IntersectionWindow::IntersectionWindow(bool manualMode)
    : m_manualMode(manualMode),
    m_nextIsEmergency(false),
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
    scene->addRect(250, 0,   100, 600, noPen, QBrush(QColor(80,80,80)));
    scene->addRect(0,   250, 600, 100, noPen, QBrush(QColor(80,80,80)));

    QPen dashPen(Qt::white, 1, Qt::DashLine);
    scene->addLine(300, 0,   300, 250, dashPen);
    scene->addLine(300, 350, 300, 600, dashPen);
    scene->addLine(0,   300, 250, 300, dashPen);
    scene->addLine(350, 300, 600, 300, dashPen);

    QPen stopPen(Qt::white, 3);
    scene->addLine(300, STOP_N, 350, STOP_N, stopPen);
    scene->addLine(250, STOP_S, 300, STOP_S, stopPen);
    scene->addLine(STOP_E, 250, STOP_E, 300, stopPen);
    scene->addLine(STOP_W, 300, STOP_W, 350, stopPen);

    // Lights on far side of intersection (standard real-world placement —
    // drivers see the light across the intersection).
    lightIndicators[0] = new DirectionalLight(0, QPointF(352, 226));   // North: top-right
    lightIndicators[1] = new DirectionalLight(1, QPointF(226, 226));   // East:  top-left
    lightIndicators[2] = new DirectionalLight(2, QPointF(226, 352));   // South: bottom-left
    lightIndicators[3] = new DirectionalLight(3, QPointF(352, 352));   // West:  bottom-right
    for (int i = 0; i < 4; i++) scene->addItem(lightIndicators[i]);

    if (!m_manualMode) buildSimulationCars();

    // HUD (both modes — shows emergency status and controls)
    QGraphicsRectItem* hudBg = scene->addRect(2, 2, 250, m_manualMode ? 56 : 42,
                                              QPen(Qt::NoPen), QBrush(QColor(0,0,0,160)));
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
    // ═══════════════════════════════════════════════════════════════════
    if (m_tickCount == 300) {
        spawnCar(0, false);
        spawnCar(0, false);
        spawnCar(2, false);
        spawnCar(2, false);
    }
    if (m_tickCount == 340) {
        spawnCar(1, false);
        spawnCar(1, false);
        spawnCar(3, false);
        spawnCar(3, false);
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

// ── Emergency center lane positions ──────────────────────────────────────
// Center of the two lanes per direction — where the emergency car spawns
static const qreal EMG_CENTER_N = 327;  // between LANE_N[0]=320 and LANE_N[1]=335
static const qreal EMG_CENTER_S = 267;  // between LANE_S[0]=260 and LANE_S[1]=275
static const qreal EMG_CENTER_E = 267;  // between LANE_E[0]=260 and LANE_E[1]=275
static const qreal EMG_CENTER_W = 327;  // between LANE_W[0]=320 and LANE_W[1]=335

// Yield positions — where lane 0 / lane 1 cars shift to when making way
static const qreal YIELD_N[2] = { 305, 347 };  // lane0 left, lane1 right
static const qreal YIELD_S[2] = { 250, 290 };
static const qreal YIELD_E[2] = { 250, 290 };
static const qreal YIELD_W[2] = { 305, 347 };

// ── Core spawn logic ──────────────────────────────────────────────────────
void IntersectionWindow::spawnCarAt(int dir, qreal spawnX, qreal spawnY, bool emergency)
{
    // Assign to the lane with fewer cars (fills lanes side by side)
    int count0 = 0, count1 = 0;
    for (CarItem* c : cars) {
        if (c->direction == dir && !c->data->isEmergency) {
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

    // Set lateral position based on lane choice
    if (emergency) {
        // Emergency cars spawn at the center of the two lanes
        bool vertical = (dir == 0 || dir == 2);
        qreal center;
        switch (dir) {
        case 0: center = EMG_CENTER_N; break;
        case 1: center = EMG_CENTER_E; break;
        case 2: center = EMG_CENTER_S; break;
        default:center = EMG_CENTER_W; break;
        }
        if (vertical) spawnX = center;
        else          spawnY = center;
    } else {
        // Normal cars: set lateral position to match chosen lane
        bool vertical = (dir == 0 || dir == 2);
        if (vertical) {
            switch (dir) {
            case 0: spawnX = LANE_N[lane]; break;
            case 2: spawnX = LANE_S[lane]; break;
            }
        } else {
            switch (dir) {
            case 1: spawnY = LANE_E[lane]; break;
            case 3: spawnY = LANE_W[lane]; break;
            }
        }
    }

    QString id = (emergency ? "EMG" : "Car") + QString::number(m_carCounter++);
    Node*    n   = new Node(id, emergency);
    CarItem* car = new CarItem(n, dir, lane);
    car->setPos(spawnX, spawnY);
    car->stopCoord     = stop;
    car->effectiveStop = stop;
    car->clearCoord    = clear;

    // Set lateral tracking (x for N/S, y for E/W)
    bool vertical = (dir == 0 || dir == 2);
    qreal lateral = vertical ? spawnX : spawnY;
    car->lateralTarget  = lateral;
    car->originalLateral = lateral;

    scene->addItem(car);
    cars.append(car);
    controller.addCar(dir, n);

    // If emergency, immediately start lane splitting
    if (emergency) {
        startLaneSplit(dir);
    }

    QString dirs[4] = {"North","East","South","West"};
    qDebug() << (emergency ? "🚨" : "🚗") << id << "from" << dirs[dir];
}

// ── Spawn one car in a direction (lane chosen by spawnCarAt) ─────────────
void IntersectionWindow::spawnCar(int dir, bool emergency)
{
    // Pass a placeholder lateral — spawnCarAt will override based on lane choice
    qreal spawnX, spawnY;
    switch (dir) {
    case 0: spawnX=LANE_N[0]; spawnY=580; break;   // lateral overridden
    case 1: spawnX=10;        spawnY=LANE_E[0]; break;
    case 2: spawnX=LANE_S[0]; spawnY=10;  break;
    default:spawnX=580;       spawnY=LANE_W[0]; break;
    }
    spawnCarAt(dir, spawnX, spawnY, emergency);
    m_nextIsEmergency = false;
    updateHud();
}

// ── HUD ───────────────────────────────────────────────────────────────────
void IntersectionWindow::updateHud()
{
    if (!m_hud) return;

    QString emergencyLine = m_nextIsEmergency
                                ? "<font color='#ff4444'>🚨 V pressed — next spawn is EMERGENCY</font><br>"
                                : "";

    QString controls = m_manualMode
                           ? "<font color='#aaaaaa'>N/E/S/W = car &nbsp;|&nbsp; V then N/E/S/W = emergency &nbsp;|&nbsp; R = restart</font>"
                           : "<font color='#aaaaaa'>V then N/E/S/W = inject emergency &nbsp;|&nbsp; R = restart</font>";

    QString carCount = m_manualMode
                           ? "<font color='#cccccc'>Cars on road: " + QString::number(cars.size()) + "</font><br>"
                           : "";

    m_hud->setHtml(carCount + emergencyLine + controls);
}

// ── Clear ─────────────────────────────────────────────────────────────────
void IntersectionWindow::clearScene()
{
    for (CarItem* car : cars) { scene->removeItem(car); delete car->data; delete car; }
    cars.clear();
    scene->clear();
    controller        = TrafficController();
    m_nextIsEmergency = false;
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
        updateHud();
        qDebug() << (m_nextIsEmergency ? "🚨 Emergency armed — press N/E/S/W"
                                       : "🚨 Emergency disarmed");
        break;

    // Direction keys — spawn car (manual) or emergency (if V was pressed)
    case Qt::Key_N:
        if (m_manualMode || m_nextIsEmergency) spawnCar(0, m_nextIsEmergency);
        break;
    case Qt::Key_E:
        if (m_manualMode || m_nextIsEmergency) spawnCar(1, m_nextIsEmergency);
        break;
    case Qt::Key_S:
        if (m_manualMode || m_nextIsEmergency) spawnCar(2, m_nextIsEmergency);
        break;
    case Qt::Key_W:
        if (m_manualMode || m_nextIsEmergency) spawnCar(3, m_nextIsEmergency);
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
}

// ── Lane splitting for emergency vehicles ────────────────────────────────
void IntersectionWindow::startLaneSplit(int dir)
{
    m_splittingDir = dir;
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
        if (car->data->isEmergency) continue;  // don't shift the emergency car itself
        if (car->inIntersection) continue;       // already through

        car->yielding = true;
        car->lateralTarget = yieldPos[car->laneIndex];
    }

    qDebug() << "🚨 Lane split started for direction" << dir;
}

void IntersectionWindow::endLaneSplit(int dir)
{
    for (CarItem* car : cars) {
        if (car->direction != dir) continue;
        if (!car->yielding) continue;

        car->yielding = false;
        car->lateralTarget = car->originalLateral;
    }

    m_splittingDir = -1;
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

    // Group non-released, non-inIntersection, NON-EMERGENCY cars by direction.
    // Emergency cars are in the center lane — they don't queue behind normal cars.
    QList<CarItem*> byDir[4];
    for (CarItem* car : cars) {
        if (!car->released && !car->inIntersection && !car->data->isEmergency)
            byDir[car->direction].append(car);
    }

    for (int d = 0; d < 4; d++) {
        QList<CarItem*>& list = byDir[d];
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

    // ── Animate lateral shifts every tick (lane splitting) ───────────
    animateAllLateral();

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

    // Only release cars if light is GREEN (not YELLOW or RED)
    for (CarItem* car : cars) {
        if (!car->released && car->atStopLine) {
            Node* ok = controller.tryRelease(car->direction);
            if (ok) { car->released = true; car->atStopLine = false; }
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
