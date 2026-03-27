#include "IntersectionWindow.h"
#include <QPen>
#include <QBrush>
#include <QKeyEvent>
#include <QGraphicsTextItem>
#include <QFont>
#include <QDebug>

static const qreal STOP_N  = 220, STOP_S  = 360, STOP_E  = 220, STOP_W  = 360;
static const qreal CLEAR_N = 250, CLEAR_S = 350, CLEAR_E = 250, CLEAR_W = 350;

static const qreal LANE_N[2] = { 320, 335 };
static const qreal LANE_S[2] = { 260, 275 };
static const qreal LANE_E[2] = { 260, 275 };
static const qreal LANE_W[2] = { 320, 335 };

// ── Constructor ───────────────────────────────────────────────────────────
IntersectionWindow::IntersectionWindow(bool manualMode)
    : m_manualMode(manualMode),
    m_nextIsEmergency(false),
    m_carCounter(0),
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

    lightIndicators[0] = new DirectionalLight(0, QPointF(352, 226));
    lightIndicators[1] = new DirectionalLight(1, QPointF(226, 226));
    lightIndicators[2] = new DirectionalLight(2, QPointF(226, 352));
    lightIndicators[3] = new DirectionalLight(3, QPointF(352, 352));
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

// ── Auto-spawn 8 cars (simulation mode) ───────────────────────────────────
void IntersectionWindow::buildSimulationCars()
{
    struct CarDef { int dir; qreal spawnX; qreal spawnY; };
    CarDef defs[8] = {
                      {0,LANE_N[0],580},{0,LANE_N[1],580},
                      {1,10,LANE_E[0]},{1,10,LANE_E[1]},
                      {2,LANE_S[0],10},{2,LANE_S[1],10},
                      {3,580,LANE_W[0]},{3,580,LANE_W[1]},
                      };
    for (int i = 0; i < 8; i++) {
        spawnCarAt(defs[i].dir, defs[i].spawnX, defs[i].spawnY, false);
    }
}

// ── Core spawn logic ──────────────────────────────────────────────────────
void IntersectionWindow::spawnCarAt(int dir, qreal spawnX, qreal spawnY, bool emergency)
{
    int lane = rand() % 2;
    qreal stop, clear;
    switch (dir) {
    case 0: stop=STOP_N; clear=CLEAR_N; break;
    case 1: stop=STOP_E; clear=CLEAR_E; break;
    case 2: stop=STOP_S; clear=CLEAR_S; break;
    default:stop=STOP_W; clear=CLEAR_W; break;
    }

    QString id = (emergency ? "EMG" : "Car") + QString::number(m_carCounter++);
    Node*    n   = new Node(id, emergency);
    CarItem* car = new CarItem(n, dir, lane);
    car->setPos(spawnX, spawnY);
    car->stopCoord  = stop;
    car->clearCoord = clear;
    scene->addItem(car);
    cars.append(car);
    controller.addCar(dir, n);

    QString dirs[4] = {"North","East","South","West"};
    qDebug() << (emergency ? "🚨" : "🚗") << id << "from" << dirs[dir];
}

// ── Spawn one car in a direction (picks lane automatically) ───────────────
void IntersectionWindow::spawnCar(int dir, bool emergency)
{
    qreal spawnX, spawnY;
    switch (dir) {
    case 0: spawnX=LANE_N[rand()%2]; spawnY=580;         break;
    case 1: spawnX=10;               spawnY=LANE_E[rand()%2]; break;
    case 2: spawnX=LANE_S[rand()%2]; spawnY=10;          break;
    default:spawnX=580;              spawnY=LANE_W[rand()%2]; break;
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
        bool green = (controller.getLightState(i) == GREEN);
        lightIndicators[i]->setColor(green ? Qt::green : Qt::red);
    }
}

// ── Per-tick update ───────────────────────────────────────────────────────
void IntersectionWindow::updateSimulation()
{
    // ── Emergency processing ──────────────────────────────────────────
    // If there are emergency vehicles queued, find the front one's CarItem
    // and release it immediately. All lights stay RED until the queue clears.
    if (controller.hasEmergency()) {
        Node* front = controller.peekEmergency();

        // Find the CarItem whose Node* matches the front of the queue
        for (CarItem* car : cars) {
            if (car->data == front && !car->released) {
                // Release this emergency car and give its lane a green flash
                controller.releaseEmergency(car->direction);
                car->released   = true;
                car->atStopLine = false;
                qDebug() << "🚑 Emergency car released visually";
                break;
            }
        }

        // Keep all other lights red while emergency is active
        updateLightVisuals();

        // Still move all cars (emergency car drives through, others stay stopped)
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
        if (m_manualMode) updateHud();
        return; // skip normal light rotation this tick
    }

    // ── Normal flow ───────────────────────────────────────────────────
    controller.advanceLights();

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
