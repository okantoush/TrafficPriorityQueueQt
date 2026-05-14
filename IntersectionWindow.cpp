#include "IntersectionWindow.h"
#include <QPen>
#include <QBrush>
#include <QKeyEvent>
#include <QGraphicsPathItem>
#include <QGraphicsTextItem>
#include <QFont>
#include <QDebug>
#include <algorithm>
#include <vector>
#include <cstddef>
#include "graphnode.h"
#include "edge.h"
#include "graphmanager.h"
#include "randomroutegenerator.h"
#include <QRandomGenerator>


// ── Tile-relative geometry ──────────────────────────────────────────────
// All constants below are RELATIVE OFFSETS within a single 600×600 tile.
// To get scene coords, add the tile's origin (originX[idx], originY[idx]).
// Each tile contains one intersection at (225,225)–(375,375) of the tile.
//   STOP_*  = where a car's leading edge stops at red.
//   CLEAR_* = where a car is "committed" — past this it ignores lights.
//   LANE_*  = top-left x (N/S) or y (E/W) of a 16-px car centered in lane.
//   TURN_*  = top-left of the dedicated left-turn lane (next to median).
//   YIELD_* = lateral position cars slide to when an emergency arrives.
//   EMG_*   = lateral position the emergency car drives at.
static const qreal STOP_N  = 385, STOP_S  = 215, STOP_E  = 215, STOP_W  = 385;
static const qreal CLEAR_N = 375, CLEAR_S = 225, CLEAR_E = 225, CLEAR_W = 375;

static const qreal LANE_N[2] = { 326, 353 };
static const qreal LANE_S[2] = { 231, 258 };
static const qreal LANE_E[2] = { 326, 353 };
static const qreal LANE_W[2] = { 231, 258 };

static const qreal TURN_N = 303, TURN_S = 281, TURN_E = 303, TURN_W = 281;

static const qreal EMG_CENTER_N = 340, EMG_CENTER_S = 244;
static const qreal EMG_CENTER_E = 340, EMG_CENTER_W = 244;

static const qreal YIELD_N[2] = { 320, 359 };
static const qreal YIELD_E[2] = { 320, 359 };
static const qreal YIELD_S[2] = { 225, 264 };
static const qreal YIELD_W[2] = { 225, 264 };

// 2×2 grid of tiles — all 600×600, touching at the seams so roads line up
// continuously across tile boundaries. The whole grid is shifted south by
// 200 px to make room for I1's north road stub above it, and the scene
// extends 200 px past the bottom of the grid for  I3/I4's south stubs.
//   I1 top-left, I2 top-right, I3 bottom-left, I4 bottom-right.
const qreal IntersectionWindow::originX[NUM_INT] = {   0, 600,   0, 600 };
const qreal IntersectionWindow::originY[NUM_INT] = { 200, 200, 800, 800 };

static const QString INT_LABELS[4] = { "I1 (top-left)", "I2 (top-right)",
                                       "I3 (bot-left)", "I4 (bot-right)" };

// ── Constructor ───────────────────────────────────────────────────────────
IntersectionWindow::IntersectionWindow(bool manualMode)
    : m_manualMode(manualMode),
      m_nextIsEmergency(false),
      m_nextIsTurnLeft(false),
      m_carCounter(0),
      m_selectedIntersection(0),
      m_tickCount(0),
      m_hud(nullptr),
      graph(nullptr)
{
    for (int i = 0; i < NUM_INT; i++) {
        m_splittingDir[i]      = -1;
        m_splitComplete[i]     = false;
        m_emergencyWaiting[i]  = false;
        m_releasedEmergency[i] = nullptr;
        for (int d = 0; d < 4; d++) {
            lightIndicators[i][d]     = nullptr;
            turnLightIndicators[i][d] = nullptr;
        }
    }

    scene = new QGraphicsScene(this);
    setScene(scene);
    setFixedSize(620, 620);

    // Scene is 1200 wide × 1600 tall:
    //   y =    0..200   north road stub above I1
    //   y =  200..1400  the 2×2 intersection grid
    //   y = 1400..1600  south road stubs below I3 (left) and I4 (right)
    // Uniform scale that fits both axes in the 620×620 viewport (height
    // wins → small grass bars on the left/right edges of the viewport).
    scene->setSceneRect(0, 0, 1200, 1600);
    setBackgroundBrush(QColor(95, 145, 90));   // grass-green between roads
    qreal s = qMin(620.0 / 1200.0, 620.0 / 1600.0);
    setTransform(QTransform().scale(s, s));
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, &IntersectionWindow::updateSimulation);

    buildScene();
    buildInitialGraph();
    timer->start(50);
}

IntersectionWindow::~IntersectionWindow()
{
    if (graph != nullptr) {
        graph->clear();
        delete graph;
        graph = nullptr;
    }
}

// ── Build scene: 4 tiles + global HUD ─────────────────────────────────────
void IntersectionWindow::buildScene()
{
    for (int i = 0; i < NUM_INT; i++) buildIntersectionTile(i);

    // ── Decorative road stubs ────────────────────────────────────────────
    // Pure visual extensions of the main road network — no intersection at
    // the far end. Cars spawned at the adjacent tile drive through these
    // before being deleted (exitN/exitS overrides set in spawnCarOnGraphEntry).
    QPen noPen(Qt::NoPen);
    QPen dashPen(Qt::white, 1, Qt::DashLine);
    QBrush road(QColor(80, 80, 80));

    // North stub above I1: continues the I1 vertical road up to scene top
    scene->addRect(225, 0, 150, 200, noPen, road);
    scene->addLine(300, 0, 300, 200, dashPen);   // median
    scene->addLine(252, 0, 252, 200, dashPen);   // S outer/inner divider
    scene->addLine(348, 0, 348, 200, dashPen);   // N inner/outer divider

    // South stub below I3: continues the I3 vertical road to scene bottom
    scene->addRect(225, 1400, 150, 200, noPen, road);
    scene->addLine(300, 1400, 300, 1600, dashPen);
    scene->addLine(252, 1400, 252, 1600, dashPen);
    scene->addLine(348, 1400, 348, 1600, dashPen);

    // South stub below I4: same idea on the right column
    scene->addRect(825, 1400, 150, 200, noPen, road);
    scene->addLine(900, 1400, 900, 1600, dashPen);   // I4 median is at x=900
    scene->addLine(852, 1400, 852, 1600, dashPen);
    scene->addLine(948, 1400, 948, 1600, dashPen);

    // Global HUD overlay (in scene coords; scaled along with everything else,
    // so we use a larger font that ends up readable after the 0.517× scale).
    QGraphicsRectItem* hudBg = scene->addRect(4, 4, 600, m_manualMode ? 205 : 185,
                                              QPen(Qt::NoPen), QBrush(QColor(0,0,0,200)));
    hudBg->setZValue(29);

    m_hud = scene->addText("");
    m_hud->setDefaultTextColor(Qt::white);
    m_hud->setFont(QFont("Helvetica", 16));
    m_hud->setPos(10, 8);
    m_hud->setZValue(30);

    updateHud();
    updateLightVisuals();

    if (!m_manualMode) buildSimulationCars();
}

// ── Build one tile's roads, lane markings, lights ────────────────────────
void IntersectionWindow::buildIntersectionTile(int idx)
{
    qreal ox = originX[idx];
    qreal oy = originY[idx];

    QPen noPen(Qt::NoPen);
    // Roads inside this tile only — N/S strip and E/W strip, both 150 px wide.
    scene->addRect(ox + 225, oy + 0,   150, 600, noPen, QBrush(QColor(80,80,80)));
    scene->addRect(ox + 0,   oy + 225, 600, 150, noPen, QBrush(QColor(80,80,80)));

    QPen dashPen(Qt::white, 1, Qt::DashLine);
    // Median center line
    scene->addLine(ox + 300, oy + 0,   ox + 300, oy + 225, dashPen);
    scene->addLine(ox + 300, oy + 375, ox + 300, oy + 600, dashPen);
    scene->addLine(ox + 0,   oy + 300, ox + 225, oy + 300, dashPen);
    scene->addLine(ox + 375, oy + 300, ox + 600, oy + 300, dashPen);
    // Lane dividers between outer/inner straight lanes.
    scene->addLine(ox + 252, oy + 0,   ox + 252, oy + 225, dashPen);
    scene->addLine(ox + 252, oy + 375, ox + 252, oy + 600, dashPen);
    scene->addLine(ox + 348, oy + 0,   ox + 348, oy + 225, dashPen);
    scene->addLine(ox + 348, oy + 375, ox + 348, oy + 600, dashPen);
    scene->addLine(ox + 0,   oy + 252, ox + 225, oy + 252, dashPen);
    scene->addLine(ox + 375, oy + 252, ox + 600, oy + 252, dashPen);
    scene->addLine(ox + 0,   oy + 348, ox + 225, oy + 348, dashPen);
    scene->addLine(ox + 375, oy + 348, ox + 600, oy + 348, dashPen);

    QPen stopPen(Qt::white, 3);
    scene->addLine(ox + 300, oy + STOP_N, ox + 375, oy + STOP_N, stopPen);
    scene->addLine(ox + 225, oy + STOP_S, ox + 300, oy + STOP_S, stopPen);
    scene->addLine(ox + STOP_E, oy + 300, ox + STOP_E, oy + 375, stopPen);
    scene->addLine(ox + STOP_W, oy + 225, ox + STOP_W, oy + 300, stopPen);

    // Yellow turn-lane boundary (approach side only)
    QPen turnLanePen(QColor(255, 200, 0), 1.5, Qt::DashLine);
    scene->addLine(ox + 320, oy + 385, ox + 320, oy + 590, turnLanePen);
    scene->addLine(ox + 280, oy + 10,  ox + 280, oy + 215, turnLanePen);
    scene->addLine(ox + 10,  oy + 320, ox + 215, oy + 320, turnLanePen);
    scene->addLine(ox + 385, oy + 280, ox + 590, oy + 280, turnLanePen);

    // Left-turn arrows on the road
    QFont arrowFont("Helvetica", 18, QFont::Bold);
    auto addArrow = [&](qreal x, qreal y, const QString& s) {
        QGraphicsTextItem* a = scene->addText(s, arrowFont);
        a->setDefaultTextColor(QColor(255, 220, 0));
        a->setPos(x, y);
    };
    addArrow(ox + TURN_N - 5, oy + 430, "↰");
    addArrow(ox + TURN_S - 5, oy + 150, "↲");
    addArrow(ox + 130,        oy + TURN_E - 14, "↱");
    addArrow(ox + 450,        oy + TURN_W - 14, "↵");

    // Tile label (small text in the corner of each tile)
    QFont labelFont("Helvetica", 14, QFont::Bold);
    QGraphicsTextItem* label = scene->addText(QString::number(idx + 1), labelFont);
    label->setDefaultTextColor(QColor(255, 255, 255, 180));
    label->setPos(ox + 555, oy + 555);
    label->setZValue(28);

    // Straight-through lights — far corners of the intersection.
    lightIndicators[idx][0] = new DirectionalLight(0, QPointF(ox + 377, oy + 201));   // N
    lightIndicators[idx][1] = new DirectionalLight(1, QPointF(ox + 377, oy + 377));   // E
    lightIndicators[idx][2] = new DirectionalLight(2, QPointF(ox + 201, oy + 377));   // S
    lightIndicators[idx][3] = new DirectionalLight(3, QPointF(ox + 201, oy + 201));   // W
    for (int d = 0; d < 4; d++) scene->addItem(lightIndicators[idx][d]);

    // Left-turn lights — beside each turn lane.
    turnLightIndicators[idx][0] = new DirectionalLight(0, QPointF(ox + 295, oy + 201));
    turnLightIndicators[idx][1] = new DirectionalLight(1, QPointF(ox + 377, oy + 295));
    turnLightIndicators[idx][2] = new DirectionalLight(2, QPointF(ox + 283, oy + 377));
    turnLightIndicators[idx][3] = new DirectionalLight(3, QPointF(ox + 201, oy + 283));
    for (int d = 0; d < 4; d++) scene->addItem(turnLightIndicators[idx][d]);
}

// ── Simulation mode: no initial cars ──────────────────────────────────────
void IntersectionWindow::buildSimulationCars() { /* spawns happen via schedule */ }

// ── Static graph overlay built during scene initialization ─────────────────
void IntersectionWindow::buildInitialGraph()
{
    // No runtime node placement exists. The city graph is hardcoded here and
    // appears immediately when the scene is created.
    //
    // This version uses a two-way lane model:
    //   - each road has two separate directed lane chains
    //   - vertical roads use x +/- laneOffset
    //   - horizontal roads use y +/- laneOffset
    //   - turns are explicit directed diagonal edges between incoming and outgoing lanes
    //
    // Old center-line graph nodes 1..28 are intentionally not used here because they
    // put cars in the middle of roads/intersections instead of on real lanes.
    graph = new GraphManager();

    const qreal laneOffset = 32.0;

    auto makeNode = [&](int id, const QPointF& pos) -> GraphNode*
    {
        GraphNode* node = new GraphNode(id, pos);
        graph->addNode(node);
        scene->addItem(node);
        return node;
    };

    auto connect = [&](GraphNode* from, GraphNode* to)
    {
        graph->connectNodes(from, to);
    };

    auto connectChain = [&](const std::vector<GraphNode*>& chain)
    {
        for (std::size_t i = 0; i + 1 < chain.size(); ++i)
        {
            connect(chain[i], chain[i + 1]);
        }
    };

    // ======================================================
    // LEFT VERTICAL ROAD, center x = 300
    // Southbound lane is west/left of the median.
    // Northbound lane is east/right of the median.
    // ======================================================
    std::vector<GraphNode*> leftSouth = {
        makeNode(101, QPointF(300 - laneOffset, 230)),
        makeNode(102, QPointF(300 - laneOffset, 400)),
        makeNode(103, QPointF(300 - laneOffset, 600)),
        makeNode(104, QPointF(300 - laneOffset, 1000)),
        makeNode(105, QPointF(300 - laneOffset, 1200)),
        makeNode(106, QPointF(300 - laneOffset, 1570))
    };

    std::vector<GraphNode*> leftNorth = {
        makeNode(107, QPointF(300 + laneOffset, 1570)),
        makeNode(108, QPointF(300 + laneOffset, 1200)),
        makeNode(109, QPointF(300 + laneOffset, 1000)),
        makeNode(110, QPointF(300 + laneOffset, 600)),
        makeNode(111, QPointF(300 + laneOffset, 400)),
        makeNode(112, QPointF(300 + laneOffset, 230))
    };

    // ======================================================
    // RIGHT VERTICAL ROAD, center x = 900
    // ======================================================
    std::vector<GraphNode*> rightSouth = {
        makeNode(113, QPointF(900 - laneOffset, 230)),
        makeNode(114, QPointF(900 - laneOffset, 400)),
        makeNode(115, QPointF(900 - laneOffset, 600)),
        makeNode(116, QPointF(900 - laneOffset, 1000)),
        makeNode(117, QPointF(900 - laneOffset, 1200)),
        makeNode(118, QPointF(900 - laneOffset, 1570))
    };

    std::vector<GraphNode*> rightNorth = {
        makeNode(119, QPointF(900 + laneOffset, 1570)),
        makeNode(120, QPointF(900 + laneOffset, 1200)),
        makeNode(121, QPointF(900 + laneOffset, 1000)),
        makeNode(122, QPointF(900 + laneOffset, 600)),
        makeNode(123, QPointF(900 + laneOffset, 400)),
        makeNode(124, QPointF(900 + laneOffset, 230))
    };

    // ======================================================
    // TOP HORIZONTAL ROAD, center y = 500
    // Eastbound lane is below the median.
    // Westbound lane is above the median.
    // ======================================================
    std::vector<GraphNode*> topEast = {
        makeNode(125, QPointF(40,   500 + laneOffset)),
        makeNode(126, QPointF(200,  500 + laneOffset)),
        makeNode(127, QPointF(400,  500 + laneOffset)),
        makeNode(128, QPointF(800,  500 + laneOffset)),
        makeNode(129, QPointF(1000, 500 + laneOffset)),
        makeNode(130, QPointF(1160, 500 + laneOffset))
    };

    std::vector<GraphNode*> topWest = {
        makeNode(131, QPointF(1160, 500 - laneOffset)),
        makeNode(132, QPointF(1000, 500 - laneOffset)),
        makeNode(133, QPointF(800,  500 - laneOffset)),
        makeNode(134, QPointF(400,  500 - laneOffset)),
        makeNode(135, QPointF(200,  500 - laneOffset)),
        makeNode(136, QPointF(40,   500 - laneOffset))
    };

    // ======================================================
    // BOTTOM HORIZONTAL ROAD, center y = 1100
    // ======================================================
    std::vector<GraphNode*> bottomEast = {
        makeNode(137, QPointF(40,   1100 + laneOffset)),
        makeNode(138, QPointF(200,  1100 + laneOffset)),
        makeNode(139, QPointF(400,  1100 + laneOffset)),
        makeNode(140, QPointF(800,  1100 + laneOffset)),
        makeNode(141, QPointF(1000, 1100 + laneOffset)),
        makeNode(142, QPointF(1160, 1100 + laneOffset))
    };

    std::vector<GraphNode*> bottomWest = {
        makeNode(143, QPointF(1160, 1100 - laneOffset)),
        makeNode(144, QPointF(1000, 1100 - laneOffset)),
        makeNode(145, QPointF(800,  1100 - laneOffset)),
        makeNode(146, QPointF(400,  1100 - laneOffset)),
        makeNode(147, QPointF(200,  1100 - laneOffset)),
        makeNode(148, QPointF(40,   1100 - laneOffset))
    };

    // ======================================================
    // Straight movement along each directed lane
    // ======================================================
    connectChain(leftSouth);
    connectChain(leftNorth);
    connectChain(rightSouth);
    connectChain(rightNorth);
    connectChain(topEast);
    connectChain(topWest);
    connectChain(bottomEast);
    connectChain(bottomWest);

    // ======================================================
    // Intersection turns
    // Each diagonal edge is directed from the incoming lane to the outgoing lane.
    // Since each road has both directions, the opposite movement is represented
    // by the matching turn edge from the opposite incoming lane.
    // ======================================================

    // Top-left intersection: center (300, 500)
    connect(leftSouth[1], topEast[2]);     // southbound left turn -> eastbound
    connect(leftSouth[1], topWest[4]);     // southbound right turn -> westbound
    connect(leftNorth[3], topWest[4]);     // northbound left turn -> westbound
    connect(leftNorth[3], topEast[2]);     // northbound right turn -> eastbound
    connect(topEast[1],   leftNorth[4]);   // eastbound left turn -> northbound
    connect(topEast[1],   leftSouth[2]);   // eastbound right turn -> southbound
    connect(topWest[3],   leftSouth[2]);   // westbound left turn -> southbound
    connect(topWest[3],   leftNorth[4]);   // westbound right turn -> northbound

    // Top-right intersection: center (900, 500)
    connect(rightSouth[1], topEast[4]);    // southbound left turn -> eastbound
    connect(rightSouth[1], topWest[2]);    // southbound right turn -> westbound
    connect(rightNorth[3], topWest[2]);    // northbound left turn -> westbound
    connect(rightNorth[3], topEast[4]);    // northbound right turn -> eastbound
    connect(topEast[3],    rightNorth[4]); // eastbound left turn -> northbound
    connect(topEast[3],    rightSouth[2]); // eastbound right turn -> southbound
    connect(topWest[1],    rightSouth[2]); // westbound left turn -> southbound
    connect(topWest[1],    rightNorth[4]); // westbound right turn -> northbound

    // Bottom-left intersection: center (300, 1100)
    connect(leftSouth[3], bottomEast[2]);  // southbound left turn -> eastbound
    connect(leftSouth[3], bottomWest[4]);  // southbound right turn -> westbound
    connect(leftNorth[1], bottomWest[4]);  // northbound left turn -> westbound
    connect(leftNorth[1], bottomEast[2]);  // northbound right turn -> eastbound
    connect(bottomEast[1], leftNorth[2]);  // eastbound left turn -> northbound
    connect(bottomEast[1], leftSouth[4]);  // eastbound right turn -> southbound
    connect(bottomWest[3], leftSouth[4]);  // westbound left turn -> southbound
    connect(bottomWest[3], leftNorth[2]);  // westbound right turn -> northbound

    // Bottom-right intersection: center (900, 1100)
    connect(rightSouth[3], bottomEast[4]); // southbound left turn -> eastbound
    connect(rightSouth[3], bottomWest[2]); // southbound right turn -> westbound
    connect(rightNorth[1], bottomWest[2]); // northbound left turn -> westbound
    connect(rightNorth[1], bottomEast[4]); // northbound right turn -> eastbound
    connect(bottomEast[3], rightNorth[2]); // eastbound left turn -> northbound
    connect(bottomEast[3], rightSouth[4]); // eastbound right turn -> southbound
    connect(bottomWest[1], rightSouth[4]); // westbound left turn -> southbound
    connect(bottomWest[1], rightNorth[2]); // westbound right turn -> northbound

    QPen edgePen(QColor(255, 245, 80), 5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    for (Edge* edge : graph->getEdges())
    {
        QGraphicsPathItem* edgeVisual = scene->addPath(edge->getPath(), edgePen);
        edgeVisual->setZValue(38);
    }
}

// ── Scripted spawn schedule (simulation mode only) ───────────────────────
// All spawns use RandomRouteGenerator (perimeter entry → paired exit on same lane).
void IntersectionWindow::processSpawnSchedule()
{
    // ACT 1: Two random graph routes
    if (m_tickCount == 1) {
        spawnCarFromGraphRandomRoute();
        spawnCarFromGraphRandomRoute();
    }
    if (m_tickCount == 20)  { spawnCarFromGraphRandomRoute(); spawnCarFromGraphRandomRoute(); }
    if (m_tickCount == 40)  { spawnCarFromGraphRandomRoute(); spawnCarFromGraphRandomRoute(); }
    if (m_tickCount == 60)  { spawnCarFromGraphRandomRoute(); spawnCarFromGraphRandomRoute(); }

    // ACT 2: Bursts of random routes (same density as old E+W per intersection)
    if (m_tickCount == 140) { for (int i = 0; i < NUM_INT; i++) spawnCarFromGraphRandomRoute(); }
    if (m_tickCount == 170) { for (int i = 0; i < NUM_INT; i++) spawnCarFromGraphRandomRoute(); }

    // ACT 3: Buildup with left-turners
    if (m_tickCount == 300) {
        spawnCarFromGraphRandomRoute(); spawnCarFromGraphRandomRoute(false, true);
        spawnCarFromGraphRandomRoute(); spawnCarFromGraphRandomRoute(false, true);
    }
    if (m_tickCount == 340) {
        spawnCarFromGraphRandomRoute(); spawnCarFromGraphRandomRoute(false, true);
        spawnCarFromGraphRandomRoute(); spawnCarFromGraphRandomRoute(false, true);
    }
    if (m_tickCount == 400) {
        for (int i = 0; i < NUM_INT; i++) {
            spawnCarFromGraphRandomRoute();
            spawnCarFromGraphRandomRoute();
        }
    }

    // ACT 4: Emergency wave
    if (m_tickCount == 520) {
        spawnCarFromGraphRandomRoute(); spawnCarFromGraphRandomRoute(); spawnCarFromGraphRandomRoute();
    }
    if (m_tickCount == 600) spawnCarFromGraphRandomRoute(true, false);

    // ACT 5: Traffic + emergency
    if (m_tickCount == 780) {
        for (int i = 0; i < NUM_INT; i++) {
            spawnCarFromGraphRandomRoute();
            spawnCarFromGraphRandomRoute();
        }
    }
    if (m_tickCount == 860) spawnCarFromGraphRandomRoute(true, false);

    // ACT 6: Steady traffic (was 4 intersections × 4 directions)
    if (m_tickCount == 1000) {
        for (int i = 0; i < NUM_INT; i++)
            for (int d = 0; d < 4; d++)
                spawnCarFromGraphRandomRoute();
    }

    // ACT 7: Emergency at south-side density
    if (m_tickCount == 1350) {
        spawnCarFromGraphRandomRoute(); spawnCarFromGraphRandomRoute(); spawnCarFromGraphRandomRoute();
    }
    if (m_tickCount == 1440) spawnCarFromGraphRandomRoute(true, false);

    // ACT 8: Periodic random graph spawns
    if (m_tickCount > 1600 && m_tickCount % 80 == 0)
        spawnCarFromGraphRandomRoute();
    if (m_tickCount > 1600 && m_tickCount % 130 == 0)
        spawnCarFromGraphRandomRoute();
}

static QPointF carTopLeftFromSceneCenter(int dir, qreal cx, qreal cy)
{
    const qreal CAR_W = 16.0, CAR_H = 28.0;
    switch (dir) {
    case 0:
    case 2: return QPointF(cx - CAR_W / 2.0, cy - CAR_H / 2.0);
    case 1:
    case 3: return QPointF(cx - CAR_H / 2.0, cy - CAR_W / 2.0);
    default: return QPointF(cx, cy);
    }
}

bool IntersectionWindow::mapGraphEntryNodeIdToTile(int entryNodeId, int& outIdx, int& outDir)
{
    switch (entryNodeId) {
    case 101: outIdx = 0; outDir = 2; return true; // I1, southbound
    case 107: outIdx = 2; outDir = 0; return true; // I3, northbound
    case 113: outIdx = 1; outDir = 2; return true; // I2, southbound
    case 119: outIdx = 3; outDir = 0; return true; // I4, northbound
    case 125: outIdx = 0; outDir = 1; return true; // I1, eastbound
    case 131: outIdx = 1; outDir = 3; return true; // I2, westbound
    case 137: outIdx = 2; outDir = 1; return true; // I3, eastbound
    case 143: outIdx = 3; outDir = 3; return true; // I4, westbound
    default: return false;
    }
}

void IntersectionWindow::spawnCarFromGraphRandomRoute(bool emergency, bool turnLeft)
{
    if (!graph) return;
    RandomRouteGenerator::StartChoice s = RandomRouteGenerator::generateRandomRoad(graph);
    if (!s.node) return;

    int destId = -1;
    if (turnLeft) {
        // Matches buildInitialGraph() protected left edges onto horizontal/vertical departures.
        switch (s.node->getID()) {
        case 101:
        case 113:
            destId = 130;
            break; // S→E, exit east top
        case 107:
        case 119:
            destId = 136;
            break; // N→W, exit west top
        case 125:
        case 137:
            destId = 112;
            break; // E→N, exit north left column
        case 131:
        case 143:
            destId = 106;
            break; // W→S, exit south left column
        default:
            return;
        }
    } else {
        RandomRouteGenerator::DestChoice d = RandomRouteGenerator::generateDestination(graph, s.roadIndex);
        if (!d.node) return;
        destId = d.node->getID();
    }
    spawnCarOnGraphEntry(s.node, destId, emergency, turnLeft);
}

void IntersectionWindow::spawnCarOnGraphEntry(GraphNode* entry, int destNodeId,
                                              bool emergency, bool turnLeft)

{
    currentEdge = entry->getOutgoingEdges()[0];

//    CarItem::t = 0.0;
//    CarItem::speed = 0.005;

    if (!entry || !graph) return;

    int idx, dir;
    if (!mapGraphEntryNodeIdToTile(entry->getID(), idx, dir))
        return;

    qreal ox = originX[idx];
    qreal oy = originY[idx];
    QPointF c = entry->getPosition();

    bool vertical = (dir == 0 || dir == 2);

    int count0 = 0, count1 = 0;
    for (CarItem* car : cars[idx]) {
        if (car->direction == dir && !car->data->isEmergency && !car->data->willTurnLeft) {
            if (car->laneIndex == 0) count0++; else count1++;
        }
    }
    int lane = (count0 <= count1) ? 0 : 1;

    qreal stop, clear;
    switch (dir) {
    case 0: stop = oy + STOP_N; clear = oy + CLEAR_N; break;
    case 1: stop = ox + STOP_E; clear = ox + CLEAR_E; break;
    case 2: stop = oy + STOP_S; clear = oy + CLEAR_S; break;
    default: stop = ox + STOP_W; clear = ox + CLEAR_W; break;
    }

    qreal finalLateral, spawnLateral;
    if (emergency) {
        qreal center = 0;
        switch (dir) {
        case 0: center = ox + EMG_CENTER_N; break;
        case 1: center = oy + EMG_CENTER_E; break;
        case 2: center = ox + EMG_CENTER_S; break;
        default: center = oy + EMG_CENTER_W; break;
        }
        finalLateral = spawnLateral = center;
    } else if (turnLeft) {
        qreal normalLateral, turnLateral;
        switch (dir) {
        case 0: normalLateral = ox + LANE_N[0]; turnLateral = ox + TURN_N; break;
        case 1: normalLateral = oy + LANE_E[0]; turnLateral = oy + TURN_E; break;
        case 2: normalLateral = ox + LANE_S[1]; turnLateral = ox + TURN_S; break;
        default: normalLateral = oy + LANE_W[1]; turnLateral = oy + TURN_W; break;
        }
        spawnLateral = normalLateral;
        finalLateral = turnLateral;
    } else {
        qreal lane0, lane1;
        switch (dir) {
        case 0: lane0 = ox + LANE_N[0]; lane1 = ox + LANE_N[1]; break;
        case 1: lane0 = oy + LANE_E[0]; lane1 = oy + LANE_E[1]; break;
        case 2: lane0 = ox + LANE_S[0]; lane1 = ox + LANE_S[1]; break;
        default: lane0 = oy + LANE_W[0]; lane1 = oy + LANE_W[1]; break;
        }
        qreal lat = vertical ? c.x() : c.y();
        lane = (qAbs(lat - lane0) <= qAbs(lat - lane1)) ? 0 : 1;
        spawnLateral = finalLateral = (lane == 0) ? lane0 : lane1;
    }

    qreal spawnX = 0;
    qreal spawnY = 0;
    if (emergency) {
        qreal cx = c.x(), cy = c.y();
        if (vertical)
            cx = (dir == 0) ? ox + EMG_CENTER_N : ox + EMG_CENTER_S;
        else
            cy = (dir == 1) ? oy + EMG_CENTER_E : oy + EMG_CENTER_W;
        QPointF tl = carTopLeftFromSceneCenter(dir, cx, cy);
        spawnX = tl.x();
        spawnY = tl.y();
    } else {
        QPointF tl = carTopLeftFromSceneCenter(dir, c.x(), c.y());
        spawnX = tl.x();
        spawnY = tl.y();
    }

    static const qreal SPAWN_GAP = 1.5;
    static const qreal CAR_LEN = 28.0;
    qreal ourLateral = vertical ? spawnX : spawnY;
    for (CarItem* car : cars[idx]) {
        if (car->direction != dir) continue;
        qreal cLat = vertical ? car->x() : car->y();
        if (qAbs(cLat - ourLateral) > 10.0) continue;
        switch (dir) {
        case 0: spawnY = qMax(spawnY, car->y() + CAR_LEN + SPAWN_GAP); break;
        case 1: spawnX = qMin(spawnX, car->x() - CAR_LEN - SPAWN_GAP); break;
        case 2: spawnY = qMin(spawnY, car->y() - CAR_LEN - SPAWN_GAP); break;
        case 3: spawnX = qMax(spawnX, car->x() + CAR_LEN + SPAWN_GAP); break;
        }
    }

    QString prefix = emergency ? "EMG" : (turnLeft ? "L" : "Car");
    QString id = prefix + QString::number(m_carCounter++);
    Node* n = new Node(id, emergency, turnLeft);
    CarItem* car = new CarItem(n, dir, lane);

    car->intersectionId = idx;
    car->originX = ox;
    car->originY = oy;
    car->exitN = (idx == 0) ? 0.0 : oy;
    car->exitS = (idx == 2 || idx == 3) ? 1640.0 : (oy + 640.0);

    GraphNode* destNode = graph->getNodeByID(destNodeId);
    if (destNode) {
        car->destGraphNodeId = destNodeId;
        if (turnLeft) {
            car->graphPostTurnExitPending = true;
            car->graphPostTurnExitScene   = destNode->getPosition();
            car->graphRouteExitEnabled    = false;
        } else {
            car->graphPostTurnExitPending = false;
            car->graphRouteExitEnabled    = true;
            car->graphExitScene           = destNode->getPosition();
        }
    } else {
        car->destGraphNodeId           = -1;
        car->graphRouteExitEnabled     = false;
        car->graphPostTurnExitPending  = false;
    }

    car->setPos(spawnX, spawnY);
    car->stopCoord = stop;
    car->effectiveStop = stop;
    car->clearCoord = clear;
    if (!turnLeft) {
        car->lateralTarget   = vertical ? spawnX : spawnY;
        car->originalLateral = car->lateralTarget;
    } else {
        car->lateralTarget   = finalLateral;
        car->originalLateral = finalLateral;
    }

    scene->addItem(car);
    cars[idx].append(car);

    if (!turnLeft) controllers[idx].addCar(dir, n);
    if (emergency) startLaneSplit(idx, dir);

    QString dirs[4] = { "N", "E", "S", "W" };
    qDebug() << (emergency ? "🚨" : (turnLeft ? "↰" : "🚗")) << id << "graph I" << (idx + 1)
             << "from" << dirs[dir] << "entry" << entry->getID() << "→ dest" << destNodeId;
}

bool IntersectionWindow::isTurnLightGreen(int idx, int dir) const
{
    return controllers[idx].getTurnLightState(dir) == GREEN;
}

// ── HUD ───────────────────────────────────────────────────────────────────
void IntersectionWindow::updateHud()
{
    if (!m_hud) return;

    int total = 0;
    for (int i = 0; i < NUM_INT; i++) total += cars[i].size();

    QString perInt;
    for (int i = 0; i < NUM_INT; i++)
        perInt += QString("I%1: %2  ").arg(i+1).arg(cars[i].size());

    QString emergencyLine = m_nextIsEmergency
        ? "<font color='#ff4444'>🚨 next spawn = EMERGENCY</font><br>" : "";
    QString turnLine = m_nextIsTurnLeft
        ? "<font color='#ffcc00'>↰ next spawn = LEFT TURN</font><br>" : "";

    QString controls = m_manualMode
        ? "<font color='#aaaaaa'>N/E/S/W or G = random graph-route car &nbsp;|&nbsp; "
          "V = next spawn emergency &nbsp;|&nbsp; L = next spawn left-turn &nbsp;|&nbsp; "
          "1–4 = HUD label only &nbsp;|&nbsp; R = restart</font>"
        : "<font color='#aaaaaa'>Simulation: all spawns use random graph routes. "
          "R = restart</font>";

    QString legend =
        "<font color='#8fd9ff'>■</font> car &nbsp;"
        "<font color='#ff6060'>■</font> emergency &nbsp;"
        "<font color='#1e90ff'>■</font> left-turner "
        "<font color='#ffcc00'>(blinker)</font><br>";

    QString carCount = "<font color='#cccccc'>Cars: " + QString::number(total)
                     + " &nbsp;(" + perInt + ")</font><br>";
    QString selected = QString("<font color='#88ff88'>Selected: %1</font><br>")
                          .arg(INT_LABELS[m_selectedIntersection]);

    QString graphLine = "<font color='#cccccc'>Graph overlay: hardcoded nodes and edges loaded at startup.</font><br>";

    m_hud->setHtml(carCount + selected + legend + graphLine + emergencyLine + turnLine + controls);
}

// ── Clear ─────────────────────────────────────────────────────────────────
void IntersectionWindow::clearScene()
{
    for (int i = 0; i < NUM_INT; i++) {
        for (CarItem* car : cars[i]) {
            scene->removeItem(car); delete car->data; delete car;
        }
        cars[i].clear();
        controllers[i] = TrafficController();
        m_splittingDir[i]      = -1;
        m_splitComplete[i]     = false;
        m_emergencyWaiting[i]  = false;
        m_releasedEmergency[i] = nullptr;
        for (int d = 0; d < 4; d++) {
            lightIndicators[i][d]     = nullptr;
            turnLightIndicators[i][d] = nullptr;
        }
    }

    // Edge objects are not QGraphicsItems, so clear them manually.
    // GraphNode items themselves are deleted by scene->clear().
    if (graph != nullptr) {
        graph->clear();
        delete graph;
        graph = nullptr;
    }
    scene->clear();

    m_nextIsEmergency  = false;
    m_nextIsTurnLeft   = false;
    m_tickCount        = 0;
    m_carCounter       = 0;
    m_hud              = nullptr;
}

void IntersectionWindow::restartSimulation()
{
    timer->stop();
    clearScene();
    buildScene();
    buildInitialGraph();
    timer->start(50);
    qDebug() << "🔄 Restarted (4 intersections)";
}

// ── Key press ─────────────────────────────────────────────────────────────
void IntersectionWindow::keyPressEvent(QKeyEvent* event)
{
    switch (event->key()) {
    case Qt::Key_R: restartSimulation(); break;

    // 1/2/3/4 — pick which intersection subsequent N/E/S/W spawns will go to.
    case Qt::Key_1: m_selectedIntersection = 0; updateHud(); break;
    case Qt::Key_2: m_selectedIntersection = 1; updateHud(); break;
    case Qt::Key_3: m_selectedIntersection = 2; updateHud(); break;
    case Qt::Key_4: m_selectedIntersection = 3; updateHud(); break;

    case Qt::Key_V:
        m_nextIsEmergency = !m_nextIsEmergency;
        if (m_nextIsEmergency) m_nextIsTurnLeft = false;
        updateHud();
        break;
    case Qt::Key_L:
        m_nextIsTurnLeft = !m_nextIsTurnLeft;
        if (m_nextIsTurnLeft) m_nextIsEmergency = false;
        updateHud();
        break;

    case Qt::Key_G:
        spawnCarFromGraphRandomRoute(m_nextIsEmergency, m_nextIsTurnLeft);
        m_nextIsEmergency = false;
        m_nextIsTurnLeft = false;
        updateHud();
        break;

    case Qt::Key_N:
    case Qt::Key_E:
    case Qt::Key_S:
    case Qt::Key_W:
        if (m_manualMode || m_nextIsEmergency || m_nextIsTurnLeft) {
            spawnCarFromGraphRandomRoute(m_nextIsEmergency, m_nextIsTurnLeft);
        }
        m_nextIsTurnLeft = false;
        updateHud();
        break;

    default: QGraphicsView::keyPressEvent(event);
    }
}

// ── Light visuals (all intersections each tick) ──────────────────────────
void IntersectionWindow::updateLightVisuals()
{
    for (int i = 0; i < NUM_INT; i++) {
        for (int d = 0; d < 4; d++) {
            LightState st = controllers[i].getLightState(d);
            QColor c = (st == GREEN) ? Qt::green : (st == YELLOW) ? Qt::yellow : Qt::red;
            if (lightIndicators[i][d]) lightIndicators[i][d]->setColor(c);

            LightState ts = controllers[i].getTurnLightState(d);
            QColor tc = (ts == GREEN) ? Qt::green : (ts == YELLOW) ? Qt::yellow : Qt::red;
            if (turnLightIndicators[i][d]) turnLightIndicators[i][d]->setColor(tc);
        }
    }
}

// ── Lane splitting (per intersection) ────────────────────────────────────
void IntersectionWindow::startLaneSplit(int idx, int dir)
{
    qreal ox = originX[idx];
    qreal oy = originY[idx];
    m_splittingDir[idx]  = dir;
    m_splitComplete[idx] = false;

    qreal yieldA, yieldB;   // yield positions for lane 0 / lane 1
    bool vertical = (dir == 0 || dir == 2);
    switch (dir) {
    case 0: yieldA = ox + YIELD_N[0]; yieldB = ox + YIELD_N[1]; break;
    case 1: yieldA = oy + YIELD_E[0]; yieldB = oy + YIELD_E[1]; break;
    case 2: yieldA = ox + YIELD_S[0]; yieldB = ox + YIELD_S[1]; break;
    default:yieldA = oy + YIELD_W[0]; yieldB = oy + YIELD_W[1]; break;
    }
    (void)vertical;

    for (CarItem* car : cars[idx]) {
        if (car->direction != dir)         continue;
        if (car->data->isEmergency)        continue;
        if (car->inIntersection)           continue;
        if (car->data->willTurnLeft)       continue;
        car->yielding      = true;
        car->lateralTarget = (car->laneIndex == 0) ? yieldA : yieldB;
    }
    qDebug() << "🚨 I" << (idx+1) << "split started, dir" << dir;
}

void IntersectionWindow::endLaneSplit(int idx, int dir)
{
    for (CarItem* car : cars[idx]) {
        if (car->direction != dir) continue;
        if (!car->yielding) continue;
        car->yielding      = false;
        car->lateralTarget = car->originalLateral;
    }
    m_splittingDir[idx]  = -1;
    m_splitComplete[idx] = false;
    qDebug() << "🚨 I" << (idx+1) << "split ended, dir" << dir;
}

bool IntersectionWindow::isLaneSplitComplete(int idx, int dir) const
{
    for (CarItem* car : cars[idx]) {
        if (car->direction != dir) continue;
        if (!car->yielding) continue;
        bool vertical = (dir == 0 || dir == 2);
        qreal current = vertical ? car->x() : car->y();
        if (qAbs(current - car->lateralTarget) > 1.0) return false;
    }
    return true;
}

void IntersectionWindow::animateAllLateral()
{
    for (int i = 0; i < NUM_INT; i++)
        for (CarItem* car : cars[i]) car->animateLateral();
}

// ── Effective stops (per intersection) ───────────────────────────────────
void IntersectionWindow::computeEffectiveStops(int idx)
{
    static const qreal GAP = 4.0;
    static const qreal CAR_LEN = 28.0;

    QList<CarItem*> byGroup[8];
    for (CarItem* car : cars[idx]) {
        if (car->released || car->inIntersection) continue;
        if (car->data->isEmergency) continue;
        int g = car->direction * 2 + (car->data->willTurnLeft ? 1 : 0);
        byGroup[g].append(car);
    }

    for (int g = 0; g < 8; g++) {
        int d = g / 2;
        QList<CarItem*>& list = byGroup[g];
        if (list.isEmpty()) continue;

        if (d == 0)      std::sort(list.begin(), list.end(), [](CarItem* a, CarItem* b){ return a->y() < b->y(); });
        else if (d == 1) std::sort(list.begin(), list.end(), [](CarItem* a, CarItem* b){ return a->x() > b->x(); });
        else if (d == 2) std::sort(list.begin(), list.end(), [](CarItem* a, CarItem* b){ return a->y() > b->y(); });
        else             std::sort(list.begin(), list.end(), [](CarItem* a, CarItem* b){ return a->x() < b->x(); });

        list[0]->effectiveStop = list[0]->stopCoord;
        for (int i = 1; i < list.size(); i++) {
            qreal aheadStop = list[i-1]->effectiveStop;
            qreal behindStop;
            if (d == 0)      behindStop = aheadStop + CAR_LEN + GAP;
            else if (d == 1) behindStop = aheadStop - CAR_LEN - GAP;
            else if (d == 2) behindStop = aheadStop - CAR_LEN - GAP;
            else             behindStop = aheadStop + CAR_LEN + GAP;

            if (d == 0)      list[i]->effectiveStop = qMax(behindStop, list[i]->stopCoord);
            else if (d == 1) list[i]->effectiveStop = qMin(behindStop, list[i]->stopCoord);
            else if (d == 2) list[i]->effectiveStop = qMin(behindStop, list[i]->stopCoord);
            else             list[i]->effectiveStop = qMax(behindStop, list[i]->stopCoord);
        }
    }

    for (CarItem* car : cars[idx]) {
        if (car->released || car->inIntersection)
            car->effectiveStop = car->stopCoord;
    }
}

bool IntersectionWindow::isIntersectionClear(int idx) const
{
    for (CarItem* car : cars[idx])
        if (car->inIntersection) return false;
    return true;
}

// ── One intersection's per-tick logic ────────────────────────────────────
void IntersectionWindow::updateOneIntersection(int idx)
{
    controllers[idx].updateCongestionStats();

    int turnCounts[4] = { 0, 0, 0, 0 };
    for (CarItem* car : cars[idx]) {
        if (!car->data->willTurnLeft) continue;
        if (car->released || car->inIntersection || car->turnCompleted) continue;
        if (car->direction < 0 || car->direction >= 4) continue;
        turnCounts[car->direction]++;
    }
    for (int d = 0; d < 4; d++) controllers[idx].setTurnQueueSize(d, turnCounts[d]);

    computeEffectiveStops(idx);

    auto removeFinished = [&](){
        QList<CarItem*> toRemove;
        for (CarItem* car : cars[idx]) if (car->moveForward()) toRemove.append(car);
        for (CarItem* car : toRemove) {
            if (car == m_releasedEmergency[idx]) m_releasedEmergency[idx] = nullptr;
            cars[idx].removeOne(car);
            scene->removeItem(car);
            delete car->data; delete car;
        }
    };

    // Emergency processing
    if (controllers[idx].hasEmergency()) {
        m_emergencyWaiting[idx] = true;

        if (m_splittingDir[idx] >= 0 && !m_splitComplete[idx]) {
            if (isLaneSplitComplete(idx, m_splittingDir[idx])) {
                m_splitComplete[idx] = true;
                qDebug() << "🚨 I" << (idx+1) << "split complete — releasing";
            }
        }
        if (m_splitComplete[idx] || m_splittingDir[idx] < 0) {
            Node* front = controllers[idx].peekEmergency();
            for (CarItem* car : cars[idx]) {
                if (car->data == front && !car->released) {
                    controllers[idx].releaseEmergency(car->direction);
                    car->released = true;
                    car->atStopLine = false;
                    m_releasedEmergency[idx] = car;
                    qDebug() << "🚑 I" << (idx+1) << "emergency released";
                    break;
                }
            }
        }
        removeFinished();
        return;
    }

    if (m_releasedEmergency[idx] != nullptr) {
        // Track until emergency exits this tile, then end split.
        QList<CarItem*> toRemove;
        for (CarItem* car : cars[idx]) if (car->moveForward()) toRemove.append(car);
        for (CarItem* car : toRemove) {
            if (car == m_releasedEmergency[idx]) {
                m_releasedEmergency[idx] = nullptr;
                if (m_splittingDir[idx] >= 0) endLaneSplit(idx, m_splittingDir[idx]);
            }
            cars[idx].removeOne(car);
            scene->removeItem(car);
            delete car->data; delete car;
        }
        if (m_releasedEmergency[idx] != nullptr) return;
    }

    m_emergencyWaiting[idx] = false;
    controllers[idx].advanceLights();

    // Release straight cars at green
    for (CarItem* car : cars[idx]) {
        if (car->data->willTurnLeft) continue;
        if (!car->released && car->atStopLine) {
            Node* ok = controllers[idx].tryRelease(car->direction);
            if (ok) { car->released = true; car->atStopLine = false; }
        }
    }
    // Release turners on protected turn-light green
    for (CarItem* car : cars[idx]) {
        if (!car->data->willTurnLeft) continue;
        if (car->released || !car->atStopLine) continue;
        if (isTurnLightGreen(idx, car->direction)) {
            car->released = true; car->atStopLine = false;
        }
    }

    removeFinished();
}

// ── Per-tick global update ────────────────────────────────────────────────
void IntersectionWindow::updateSimulation()
{
    m_tickCount++;
    if (!m_manualMode)
        processSpawnSchedule();

    animateAllLateral();
    for (int i = 0; i < NUM_INT; i++)
        for (CarItem* car : cars[i]) car->updateBlinker();

    for (int i = 0; i < NUM_INT; i++) updateOneIntersection(i);

    updateLightVisuals();
    updateHud();
}



