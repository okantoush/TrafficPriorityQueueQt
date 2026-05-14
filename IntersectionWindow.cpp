#include "IntersectionWindow.h"
#include <QPen>
#include <QBrush>
#include <QKeyEvent>
#include <QGraphicsPathItem>
#include <QGraphicsTextItem>
#include <QGraphicsProxyWidget>
#include <QFont>
#include <QDebug>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
#include <vector>
#include <cstddef>
#include "graphnode.h"
#include "edge.h"
#include "graphmanager.h"
#include "randomroutegenerator.h"
#include <QRandomGenerator>
#include <QLineF>
#include <QtMath>


// ── Tile-relative geometry ──────────────────────────────────────────────
// Constants below are RELATIVE OFFSETS within a 600×600 tile, used by
// buildIntersectionTile() to draw stop lines and turn arrows. To get scene
// coords add the tile's origin (originX[idx], originY[idx]). Each tile
// contains one intersection at (225,225)–(375,375) of the tile.
//   STOP_*  = stop-line scene coord for a car's leading edge at red.
//   TURN_*  = top-left of the dedicated left-turn lane (next to median).
//   YIELD_* = lateral position cars slide to during an emergency lane split
//             (still referenced by startLaneSplit / endLaneSplit, currently
//             unused under Dijkstra path-following but kept for the rendered
//             yellow boundary line).
static const qreal STOP_N  = 385, STOP_S  = 215, STOP_E  = 215, STOP_W  = 385;
static const qreal TURN_N  = 303, TURN_S  = 281, TURN_E  = 303, TURN_W  = 281;

static const qreal YIELD_N[2] = { 320, 359 };
static const qreal YIELD_E[2] = { 320, 359 };
static const qreal YIELD_S[2] = { 225, 264 };
static const qreal YIELD_W[2] = { 225, 264 };

// Tile-grid layout. rows × cols tiles of 600×600 each, packed tight so the
// roads connect at the seams. Sandwiched between a 200-px stub above the
// top row and a 200-px stub below the bottom row (so cars driving N/S have
// road to disappear into past the perimeter intersections). No E/W stubs —
// horizontal chains start/end at x = ±40 of the scene edge directly.
//
// Per intersection idx = r*cols + c:
//   originX[idx] = c * 600
//   originY[idx] = 200 + r * 600
//   Intersection center: (originX + 300, originY + 300)
//
// Scene: cols * 600  wide  ×  rows * 600 + 400  tall.

// ── Constructor ───────────────────────────────────────────────────────────
IntersectionWindow::IntersectionWindow(bool manualMode, int rows, int cols)
    : m_rows(qMax(1, rows)),
      m_cols(qMax(1, cols)),
      m_numIntersections(qMax(1, rows) * qMax(1, cols)),
      m_manualMode(manualMode),
      m_nextIsEmergency(false),
      m_nextIsTurnLeft(false),
      m_carCounter(0),
      m_selectedIntersection(0),
      m_tickCount(0),
      m_hud(nullptr),
      m_emergencyButtonProxy(nullptr),
      m_dijStage(DIJ_IDLE),
      m_dijStart(nullptr),
      m_dijEnd(nullptr),
      m_dijStepCounter(0),
      m_dijDoneTimer(0),
      m_dijHud(nullptr),
      m_dijHudBg(nullptr),
      graph(nullptr)
{
    // Size every per-intersection vector to numIntersections.
    originX.resize(m_numIntersections);
    originY.resize(m_numIntersections);
    controllers.resize(m_numIntersections);
    cars.resize(m_numIntersections);
    lightIndicators.resize(m_numIntersections);
    turnLightIndicators.resize(m_numIntersections);
    for (int i = 0; i < m_numIntersections; i++) {
        lightIndicators[i].fill(nullptr, 4);
        turnLightIndicators[i].fill(nullptr, 4);
    }
    m_splittingDir.fill(-1, m_numIntersections);
    m_splitComplete.fill(false, m_numIntersections);
    m_emergencyWaiting.fill(false, m_numIntersections);
    m_releasedEmergency.fill(nullptr, m_numIntersections);

    // Compute per-tile origins (row-major).
    for (int r = 0; r < m_rows; r++) {
        for (int c = 0; c < m_cols; c++) {
            int idx = r * m_cols + c;
            originX[idx] = c * 600.0;
            originY[idx] = 200.0 + r * 600.0;
        }
    }

    // Scene size from grid dimensions.
    m_sceneWidth  = m_cols * 600.0;
    m_sceneHeight = m_rows * 600.0 + 400.0;

    scene = new QGraphicsScene(this);
    setScene(scene);
    setFixedSize(620, 620);

    scene->setSceneRect(0, 0, m_sceneWidth, m_sceneHeight);
    setBackgroundBrush(QColor(95, 145, 90));
    qreal s = qMin(620.0 / m_sceneWidth, 620.0 / m_sceneHeight);
    setTransform(QTransform().scale(s, s));
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, &IntersectionWindow::updateSimulation);

    buildScene();
    buildInitialGraph();
    if (m_manualMode) buildDijkstraHud();
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

// ── Build scene: every tile + perimeter N/S stubs + HUD ─────────────────
void IntersectionWindow::buildScene()
{
    for (int i = 0; i < m_numIntersections; i++) buildIntersectionTile(i);

    // North / south stubs — pure visual extensions of the vertical roads
    // past the top and bottom rows, so cars driving N/S have road to drive
    // into past the perimeter intersection. One stub per top-row column
    // and one per bottom-row column.
    QPen   noPen(Qt::NoPen);
    QPen   dashPen(Qt::white, 1, Qt::DashLine);
    QBrush road(QColor(80, 80, 80));

    const qreal stubH      = 200.0;
    const qreal bottomYTop = m_sceneHeight - stubH;   // y where bottom stubs begin

    for (int c = 0; c < m_cols; c++) {
        qreal ox = c * 600.0;
        // North stub above top-row col c
        scene->addRect(ox + 225, 0, 150, stubH, noPen, road);
        scene->addLine(ox + 300, 0, ox + 300, stubH, dashPen);
        scene->addLine(ox + 252, 0, ox + 252, stubH, dashPen);
        scene->addLine(ox + 348, 0, ox + 348, stubH, dashPen);
        // South stub below bottom-row col c
        scene->addRect(ox + 225, bottomYTop, 150, stubH, noPen, road);
        scene->addLine(ox + 300, bottomYTop, ox + 300, m_sceneHeight, dashPen);
        scene->addLine(ox + 252, bottomYTop, ox + 252, m_sceneHeight, dashPen);
        scene->addLine(ox + 348, bottomYTop, ox + 348, m_sceneHeight, dashPen);
    }

    // Global HUD overlay (scaled with the scene, so the font ends up readable
    // after the viewport-fit transform).
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
    buildEmergencyButton();

    if (!m_manualMode) buildSimulationCars();
}

void IntersectionWindow::buildEmergencyButton()
{
    if (!scene) return;

    QPushButton* btn = new QPushButton(QStringLiteral("🚑 Smart Ambulance"));
    btn->setCursor(Qt::PointingHandCursor);
    btn->setToolTip(QStringLiteral("Choose emergency type, select ambulance spawn node, and route to the best ready hospital."));
    btn->setStyleSheet(
        "QPushButton {"
        " background:#d64545; color:white; border:1px solid #ffaaaa;"
        " border-radius:8px; padding:6px 10px; font-weight:bold;"
        " font-size:13px;"
        "}"
        "QPushButton:hover { background:#ef5a5a; }"
        "QPushButton:pressed { background:#a92f2f; }"
    );
    btn->setMinimumWidth(180);

    connect(btn, &QPushButton::clicked, this, [this]() {
        openEmergencyDialog();
    });

    m_emergencyButtonProxy = scene->addWidget(btn);
    m_emergencyButtonProxy->setZValue(100);
    m_emergencyButtonProxy->setPos(410, m_manualMode ? 160 : 140);
}


// Friendly tile label for the HUD selector — e.g. "I3 (r1c0)".
QString IntersectionWindow::tileLabel(int idx) const
{
    if (idx < 0 || idx >= m_numIntersections) return QString::number(idx + 1);
    int r = idx / m_cols;
    int c = idx % m_cols;
    return QString("I%1 (r%2 c%3)").arg(idx + 1).arg(r).arg(c);
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

// ── Graph generator for an arbitrary rows × cols grid ───────────────────
//
// For each ROW: one horizontal road with two directed chains (E + W).
//   Each chain has 2 + 2*cols nodes:
//     [perimeter entry] [appr c0] [post c0] [appr c1] [post c1] … [perim exit]
//
// For each COL: one vertical road with two directed chains (S + N).
//   Each chain has 2 + 2*rows nodes (same pattern, swapped axis).
//
// At every intersection (r, c) we add 8 turn edges connecting the chains'
// approach nodes to the other chains' post nodes. The approach index in
// each chain for intersection (r, c) is:
//     SB chain (top → bottom): vertS[c][ 2*r + 1 ]
//     NB chain (bottom → top): vertN[c][ 2*(rows-r-1) + 1 ]
//     EB chain (left → right): horizE[r][ 2*c + 1 ]
//     WB chain (right → left): horizW[r][ 2*(cols-c-1) + 1 ]
// The post index is just +1 from the approach in the same chain.
//
// All static lookup tables (approach → intersection/dir, node → chain,
// chain → direction, perimeter entry/exit lists) are populated into
// m_graphInfo here so CarItem's static helpers work on any grid size.
void IntersectionWindow::buildInitialGraph()
{
    graph = new GraphManager();
    m_graphInfo.clear();
    CarItem::graphInfo = &m_graphInfo;

    const qreal laneOffset  = 32.0;
    const qreal cellSize    = 600.0;
    const qreal tileHalf    = 300.0;   // intersection center offset within tile
    const qreal apprDist    = 100.0;   // approach/post offset from intersection center
    const qreal perimMargin = 40.0;    // horizontal perimeter inset
    const qreal vertPerimTop    = 230.0;                  // SB entry / NB exit
    const qreal vertPerimBottom = m_sceneHeight - 30.0;   // SB exit / NB entry

    int nextNodeId  = 101;
    int nextChainId = 0;

    auto makeNode = [&](QPointF pos) -> GraphNode* {
        GraphNode* node = new GraphNode(nextNodeId++, pos);
        graph->addNode(node);
        scene->addItem(node);
        return node;
    };
    auto registerChain = [&](const QVector<GraphNode*>& chain, int chainId, int dir) {
        for (int i = 0; i + 1 < chain.size(); ++i)
            graph->connectNodes(chain[i], chain[i + 1]);
        for (GraphNode* n : chain) m_graphInfo.nodeChain[n->getID()] = chainId;
        m_graphInfo.chainDirection[chainId] = dir;
    };

    // Storage for chain references — used a moment later to wire up turn
    // edges between approach and post nodes at every intersection.
    QVector<QVector<GraphNode*>> horizE(m_rows);
    QVector<QVector<GraphNode*>> horizW(m_rows);
    QVector<QVector<GraphNode*>> vertS(m_cols);
    QVector<QVector<GraphNode*>> vertN(m_cols);

    // ── Horizontal chains (one E + one W per row) ───────────────────
    for (int r = 0; r < m_rows; r++) {
        qreal centerY = 200.0 + r * cellSize + tileHalf;
        int tileLeftIdx  = r * m_cols + 0;
        int tileRightIdx = r * m_cols + (m_cols - 1);

        // Eastbound chain: left perim → c0 appr → c0 post → c1 appr → … → right perim
        QVector<GraphNode*> chainE;
        qreal y_e = centerY + laneOffset;
        chainE.append(makeNode(QPointF(perimMargin, y_e)));   // entry (left)
        for (int c = 0; c < m_cols; c++) {
            qreal centerX = c * cellSize + tileHalf;
            chainE.append(makeNode(QPointF(centerX - apprDist, y_e)));  // approach
            chainE.append(makeNode(QPointF(centerX + apprDist, y_e)));  // post
        }
        chainE.append(makeNode(QPointF(m_sceneWidth - perimMargin, y_e)));   // exit
        int cidE = nextChainId++;
        registerChain(chainE, cidE, 1);
        horizE[r] = chainE;
        m_graphInfo.entryNodes.append(chainE.first()->getID());
        m_graphInfo.exitNodes.append(chainE.last()->getID());
        m_graphInfo.entryToTile[chainE.first()->getID()] = tileLeftIdx;
        for (int c = 0; c < m_cols; c++) {
            int intId = r * m_cols + c;
            m_graphInfo.approachNodes[chainE[2 * c + 1]->getID()] = { intId, 1 };
        }

        // Westbound chain: right perim → cN-1 appr → cN-1 post → … → left perim
        QVector<GraphNode*> chainW;
        qreal y_w = centerY - laneOffset;
        chainW.append(makeNode(QPointF(m_sceneWidth - perimMargin, y_w)));   // entry (right)
        for (int c = m_cols - 1; c >= 0; c--) {
            qreal centerX = c * cellSize + tileHalf;
            chainW.append(makeNode(QPointF(centerX + apprDist, y_w)));   // approach (east side of intersection going W)
            chainW.append(makeNode(QPointF(centerX - apprDist, y_w)));   // post     (west side)
        }
        chainW.append(makeNode(QPointF(perimMargin, y_w)));                  // exit (left)
        int cidW = nextChainId++;
        registerChain(chainW, cidW, 3);
        horizW[r] = chainW;
        m_graphInfo.entryNodes.append(chainW.first()->getID());
        m_graphInfo.exitNodes.append(chainW.last()->getID());
        m_graphInfo.entryToTile[chainW.first()->getID()] = tileRightIdx;
        for (int c = 0; c < m_cols; c++) {
            int intId = r * m_cols + c;
            int wApprIdx = 2 * (m_cols - c - 1) + 1;
            m_graphInfo.approachNodes[chainW[wApprIdx]->getID()] = { intId, 3 };
        }
    }

    // ── Vertical chains (one S + one N per column) ──────────────────
    for (int c = 0; c < m_cols; c++) {
        qreal centerX = c * cellSize + tileHalf;
        int tileTopIdx    = 0 * m_cols + c;
        int tileBottomIdx = (m_rows - 1) * m_cols + c;

        // Southbound chain: top perim → r0 appr → r0 post → … → bottom perim
        QVector<GraphNode*> chainS;
        qreal x_s = centerX - laneOffset;
        chainS.append(makeNode(QPointF(x_s, vertPerimTop)));
        for (int r = 0; r < m_rows; r++) {
            qreal centerY = 200.0 + r * cellSize + tileHalf;
            chainS.append(makeNode(QPointF(x_s, centerY - apprDist)));
            chainS.append(makeNode(QPointF(x_s, centerY + apprDist)));
        }
        chainS.append(makeNode(QPointF(x_s, vertPerimBottom)));
        int cidS = nextChainId++;
        registerChain(chainS, cidS, 2);
        vertS[c] = chainS;
        m_graphInfo.entryNodes.append(chainS.first()->getID());
        m_graphInfo.exitNodes.append(chainS.last()->getID());
        m_graphInfo.entryToTile[chainS.first()->getID()] = tileTopIdx;
        for (int r = 0; r < m_rows; r++) {
            int intId = r * m_cols + c;
            m_graphInfo.approachNodes[chainS[2 * r + 1]->getID()] = { intId, 2 };
        }

        // Northbound chain: bottom perim → rN-1 appr → rN-1 post → … → top perim
        QVector<GraphNode*> chainN;
        qreal x_n = centerX + laneOffset;
        chainN.append(makeNode(QPointF(x_n, vertPerimBottom)));
        for (int r = m_rows - 1; r >= 0; r--) {
            qreal centerY = 200.0 + r * cellSize + tileHalf;
            chainN.append(makeNode(QPointF(x_n, centerY + apprDist)));
            chainN.append(makeNode(QPointF(x_n, centerY - apprDist)));
        }
        chainN.append(makeNode(QPointF(x_n, vertPerimTop)));
        int cidN = nextChainId++;
        registerChain(chainN, cidN, 0);
        vertN[c] = chainN;
        m_graphInfo.entryNodes.append(chainN.first()->getID());
        m_graphInfo.exitNodes.append(chainN.last()->getID());
        m_graphInfo.entryToTile[chainN.first()->getID()] = tileBottomIdx;
        for (int r = 0; r < m_rows; r++) {
            int intId = r * m_cols + c;
            int nApprIdx = 2 * (m_rows - r - 1) + 1;
            m_graphInfo.approachNodes[chainN[nApprIdx]->getID()] = { intId, 0 };
        }
    }

    // ── Turn edges at every intersection (r, c). 8 edges each:
    //   SB:  left→E,   right→W
    //   NB:  left→W,   right→E
    //   EB:  left→N,   right→S
    //   WB:  left→S,   right→N
    for (int r = 0; r < m_rows; r++) {
        for (int c = 0; c < m_cols; c++) {
            const int sAppr = 2 * r + 1;
            const int sPost = 2 * r + 2;
            const int nAppr = 2 * (m_rows - r - 1) + 1;
            const int nPost = 2 * (m_rows - r - 1) + 2;
            const int eAppr = 2 * c + 1;
            const int ePost = 2 * c + 2;
            const int wAppr = 2 * (m_cols - c - 1) + 1;
            const int wPost = 2 * (m_cols - c - 1) + 2;

            GraphNode* sApprN = vertS [c][sAppr];
            GraphNode* nApprN = vertN [c][nAppr];
            GraphNode* eApprN = horizE[r][eAppr];
            GraphNode* wApprN = horizW[r][wAppr];
            GraphNode* sPostN = vertS [c][sPost];
            GraphNode* nPostN = vertN [c][nPost];
            GraphNode* ePostN = horizE[r][ePost];
            GraphNode* wPostN = horizW[r][wPost];

            graph->connectNodes(sApprN, ePostN);   // SB left
            graph->connectNodes(sApprN, wPostN);   // SB right
            graph->connectNodes(nApprN, wPostN);   // NB left
            graph->connectNodes(nApprN, ePostN);   // NB right
            graph->connectNodes(eApprN, nPostN);   // EB left
            graph->connectNodes(eApprN, sPostN);   // EB right
            graph->connectNodes(wApprN, sPostN);   // WB left
            graph->connectNodes(wApprN, nPostN);   // WB right
        }
    }

    // ── Hospitals: extra graph nodes in the grass "squares" (no road) ───────
    // Each 600×600 tile has four corner grass quadrants outside the 150-wide
    // N/S and E/W road strips. We place hospitals there—not on the asphalt
    // at the intersection center—and connect to the nearest post node.
    {
        auto hospitalCountForGrid = [](int rows, int cols) -> int {
            const int a = qMin(rows, cols);
            const int b = qMax(rows, cols);
            if (a == 2 && b == 2) return 1;
            if (a == 2 && b == 3) return 2;
            if (a == 3 && b == 3) return 2;
            if (a == 3 && b == 4) return 2;
            if (a == 4 && b == 3) return 2;
            if (a == 4 && b == 4) return 3;
            const int tiles = rows * cols;
            return qBound(1, (tiles + 2) / 5, tiles);
        };

        static const char* const kHospitalNames[] = {
            "Kasr Al Ainy ER", "Ain Shams Hospital", "Sheikh Zayed Specialized",
            "Alexandria Main Hospital", "Mansoura Emergency",
            "Assiut University Hospital", "Zagazig University Hospital"
        };
        const int nHospitalNames = int(sizeof(kHospitalNames) / sizeof(kHospitalNames[0]));

        const int nh = hospitalCountForGrid(m_rows, m_cols);
        auto distributedTiles = [](int k, int numTiles) -> QVector<int> {
            QVector<int> out;
            if (k <= 0 || numTiles <= 0) return out;
            if (k == 1) {
                out.append(numTiles / 2);
                return out;
            }
            for (int i = 0; i < k; ++i)
                out.append((i * (numTiles - 1)) / (k - 1));
            return out;
        };

        QVector<int> generatedHospitalNodeIds;
        const QVector<int> tilePick = distributedTiles(nh, m_numIntersections);
        for (int hi = 0; hi < tilePick.size(); ++hi) {
            const int tIdx = tilePick[hi];
            if (tIdx < 0 || tIdx >= m_numIntersections) continue;

            const int rr = tIdx / m_cols;
            const int cc = tIdx % m_cols;
            const qreal ox = originX[tIdx];
            const qreal oy = originY[tIdx];
            // Corner grass plots (roads occupy x∈[225,375] and y∈[225,375] in tile space).
            // inset keeps the marker clearly inside the green quadrant, away from kerbs.
            // Large hospital marker + label need room inside the ~225px grass quadrant.
            const qreal inset = 108.0;
            const int corner = (hi + tIdx * 3) % 4;   // spread corners across tiles / indices
            QPointF hpos;
            switch (corner) {
            case 0: hpos = QPointF(ox + inset, oy + inset); break;                 // NW grass
            case 1: hpos = QPointF(ox + 600.0 - inset, oy + inset); break;         // NE
            case 2: hpos = QPointF(ox + inset, oy + 600.0 - inset); break;         // SW
            default: hpos = QPointF(ox + 600.0 - inset, oy + 600.0 - inset); break; // SE
            }

            const int sPostIdx = 2 * rr + 2;
            const int nPostIdx = 2 * (m_rows - rr - 1) + 2;
            const int ePostIdx = 2 * cc + 2;
            const int wPostIdx = 2 * (m_cols - cc - 1) + 2;

            GraphNode* cand[4] = {
                vertS[cc][sPostIdx],
                vertN[cc][nPostIdx],
                horizE[rr][ePostIdx],
                horizW[rr][wPostIdx],
            };
            GraphNode* attach = cand[0];
            qreal bestLen = QLineF(hpos, attach->getPosition()).length();
            for (int i = 1; i < 4; ++i) {
                const qreal len = QLineF(hpos, cand[i]->getPosition()).length();
                if (len < bestLen) {
                    bestLen = len;
                    attach = cand[i];
                }
            }

            const QString hname = QString::fromUtf8(kHospitalNames[hi % nHospitalNames]);
            GraphNode* hosp = new GraphNode(nextNodeId++, hpos, hname);
            graph->addNode(hosp);
            scene->addItem(hosp);
            graph->connectNodes(attach, hosp);
            graph->connectNodes(hosp, attach);
            generatedHospitalNodeIds.append(hosp->getID());
        }

        m_hospitalManager.initializeDefaultHospitals(generatedHospitalNodeIds);
    }

    // Faint yellow overlay for every edge. The QGraphicsPathItem pointer
    // for each edge is stashed in m_edgeVisuals so the Dijkstra animation
    // can recolor specific edges later (tree edges in orange, the final
    // path in cyan) without affecting the others.
    QPen edgePen(QColor(255, 245, 80, 60), 4,
                 Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    m_edgeVisuals.clear();
    for (Edge* edge : graph->getEdges()) {
        QGraphicsPathItem* edgeVisual = scene->addPath(edge->getPath(), edgePen);
        edgeVisual->setZValue(4);
        m_edgeVisuals.insert(edge, edgeVisual);
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
    if (m_tickCount == 140) { for (int i = 0; i < m_numIntersections; i++) spawnCarFromGraphRandomRoute(); }
    if (m_tickCount == 170) { for (int i = 0; i < m_numIntersections; i++) spawnCarFromGraphRandomRoute(); }

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
        for (int i = 0; i < m_numIntersections; i++) {
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
        for (int i = 0; i < m_numIntersections; i++) {
            spawnCarFromGraphRandomRoute();
            spawnCarFromGraphRandomRoute();
        }
    }
    if (m_tickCount == 860) spawnCarFromGraphRandomRoute(true, false);

    // ACT 6: Steady traffic (was 4 intersections × 4 directions)
    if (m_tickCount == 1000) {
        for (int i = 0; i < m_numIntersections; i++)
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

// For each intersection, find an emergency vehicle that's approaching or
// crossing it and force the appropriate light green for its direction
// (turn light for a left-turning emergency, straight light for straight /
// right). If none is present, clear any pre-existing override so the
// normal phase cycle resumes.
//
// "Approaching" = the emergency's NEXT waypoint is an approach node into
// this intersection.  "Crossing" = the waypoint it most recently passed
// was an approach into this intersection (i.e. the car is mid-Bezier or
// mid-straight-crossing right now).
void IntersectionWindow::propagateEmergencyOverrides()
{
    for (int idx = 0; idx < m_numIntersections; idx++) {
        bool found  = false;
        int  eDir   = -1;
        int  eIntent = 0;

        for (int srcTile = 0; srcTile < m_numIntersections && !found; srcTile++) {
            for (CarItem* car : cars[srcTile]) {
                if (!car->data->isEmergency)        continue;
                if (car->pathWaypoints.isEmpty())   continue;
                if (car->pathCursor < 0)            continue;
                if (car->pathCursor > car->pathNodeIds.size()) continue;

                int approachIdx = -1;
                int intId = -1, dir = -1;

                // Approaching: next target is an approach into this intersection.
                if (car->pathCursor < car->pathNodeIds.size()) {
                    int nid = car->pathNodeIds[car->pathCursor];
                    if (CarItem::approachNodeInfo(nid, intId, dir) && intId == idx)
                        approachIdx = car->pathCursor;
                }
                // Crossing: just left an approach into this intersection.
                if (approachIdx < 0 && car->pathCursor - 1 >= 0 &&
                    car->pathCursor - 1 < car->pathNodeIds.size())
                {
                    int prevNid = car->pathNodeIds[car->pathCursor - 1];
                    int pIntId = -1, pDir = -1;
                    if (CarItem::approachNodeInfo(prevNid, pIntId, pDir) && pIntId == idx) {
                        approachIdx = car->pathCursor - 1;
                        intId = pIntId;
                        dir   = pDir;
                    }
                }
                if (approachIdx < 0) continue;

                // Intent at THIS approach picks straight vs turn light.
                int intent = 0;
                if (approachIdx + 1 < car->pathNodeIds.size()) {
                    intent = CarItem::turnIntentForApproach(
                        car->pathNodeIds[approachIdx],
                        car->pathNodeIds[approachIdx + 1],
                        dir);
                }

                eDir    = dir;
                eIntent = intent;
                found   = true;
                break;
            }
        }

        if (found) {
            bool useTurnLight = (eIntent == 1);
            controllers[idx].setEmergencyOverride(eDir, useTurnLight);
        } else {
            controllers[idx].clearEmergencyOverride();
        }
    }
}

// One pass per tick:
//   1) For every path car, set stopAtNextWaypoint based on its INTENT at
//      its next approach (left-turners check the protected turn light,
//      everyone else checks the straight light).
//   2) Group blocked cars by (approachNodeId, lane) and rank them by
//      direction-distance to the approach. Front car (rank 0) stops AT
//      the approach waypoint; cars behind get pathStopPos shifted back
//      one CAR_LEN+GAP per rank so a queue stacks with proper spacing
//      instead of piling onto a single point.
//   3) Push split queue counts (straight vs turn) to each controller so
//      calculateGreenTicks can extend the correct phase.
void IntersectionWindow::updatePathCarStopFlags()
{
    struct StopGroup {
        int            intId;
        int            dir;
        QPointF        approachPos;
        QList<CarItem*> cars;
    };
    // 16 approach nodes × 3 lanes = max 48 groups. A QHash keyed on
    // (nodeId * 3 + lane) is plenty.
    QHash<int, StopGroup> groups;

    QVector<QVector<int>> straightCounts(m_numIntersections, QVector<int>(4, 0));
    QVector<QVector<int>> turnCounts    (m_numIntersections, QVector<int>(4, 0));

    for (int idx = 0; idx < m_numIntersections; idx++) {
        for (CarItem* car : cars[idx]) {
            if (car->pathWaypoints.isEmpty()) continue;
            if (car->pathCursor >= car->pathNodeIds.size()) {
                car->stopAtNextWaypoint = false;
                continue;
            }

            // Emergency vehicles never stop: they preempt the lights they
            // pass through via setEmergencyOverride (propagated each tick
            // by propagateEmergencyOverrides). Skip them in the queue
            // accounting too — they're not "waiting", they're plowing.
            if (car->data->isEmergency) {
                car->stopAtNextWaypoint = false;
                continue;
            }

            int nextNodeId = car->pathNodeIds[car->pathCursor];
            int intId = -1, dir = -1;
            if (!CarItem::approachNodeInfo(nextNodeId, intId, dir)) {
                car->stopAtNextWaypoint = false;
                continue;
            }

            // Intent at THIS approach — picks the correct light.
            int intent = 0;
            if (car->pathCursor + 1 < car->pathNodeIds.size()) {
                intent = CarItem::turnIntentForApproach(
                    nextNodeId, car->pathNodeIds[car->pathCursor + 1], dir);
            }
            LightState s = (intent == 1)
                ? controllers[intId].getTurnLightState(dir)
                : controllers[intId].getLightState(dir);
            bool blocked = (s != GREEN);
            car->stopAtNextWaypoint = blocked;

            if (!blocked) continue;

            // Group this waiting car by (approach, lane). Lane is
            // determined by intent: left→0, right→2, straight→1.
            int lane = (intent == 1) ? 0 : (intent == 2 ? 2 : 1);
            int key  = nextNodeId * 3 + lane;
            StopGroup& g = groups[key];
            g.intId       = intId;
            g.dir         = dir;
            g.approachPos = car->pathWaypoints[car->pathCursor];
            g.cars.append(car);

            if (intent == 1) turnCounts[intId][dir]++;
            else             straightCounts[intId][dir]++;
        }
    }

    // For each group: sort by direction-distance to approach (front first)
    // and write per-car pathStopPos = approach + rank * (CAR_LEN+GAP)
    // shifted AWAY from the direction of travel.
    static const qreal CAR_LEN = 28.0;
    static const qreal GAP     = 4.0;
    static const qreal STEP    = CAR_LEN + GAP;

    for (auto it = groups.begin(); it != groups.end(); ++it) {
        StopGroup& g = it.value();
        const int dir = g.dir;

        std::sort(g.cars.begin(), g.cars.end(),
                  [dir](CarItem* a, CarItem* b) {
            QPointF ac = a->sceneCenter();
            QPointF bc = b->sceneCenter();
            switch (dir) {
            case 0: return ac.y() < bc.y();   // N: smaller y is closer to approach
            case 1: return ac.x() > bc.x();   // E: larger  x is closer
            case 2: return ac.y() > bc.y();   // S: larger  y is closer
            case 3: return ac.x() < bc.x();   // W: smaller x is closer
            }
            return false;
        });

        for (int r = 0; r < g.cars.size(); r++) {
            QPointF stop = g.approachPos;
            qreal back = r * STEP;
            switch (dir) {
            case 0: stop.setY(stop.y() + back); break;   // N → behind is south
            case 1: stop.setX(stop.x() - back); break;   // E → behind is west
            case 2: stop.setY(stop.y() - back); break;   // S → behind is north
            case 3: stop.setX(stop.x() + back); break;   // W → behind is east
            }
            g.cars[r]->pathStopPos = stop;
        }
    }

    for (int idx = 0; idx < m_numIntersections; idx++) {
        for (int d = 0; d < 4; d++) {
            controllers[idx].setStraightQueueSize(d, straightCounts[idx][d]);
            controllers[idx].setTurnQueueSize    (d, turnCounts[idx][d]);
        }
    }
}

// ── Dijkstra-driven random spawn ────────────────────────────────────────
// Pick two distinct perimeter nodes (one entry, one exit), find the shortest
// path between them with Dijkstra, and hand that path to a new CarItem. The
// car drives the path waypoint-by-waypoint via CarItem::movePath().
//
// `emergency` and `turnLeft` are kept for HUD/visual flagging (color, blinker)
// but no longer steer the route — Dijkstra picks whatever the shortest path
// happens to be, which may or may not include a left turn.
void IntersectionWindow::spawnCarFromGraphRandomRoute(bool emergency, bool turnLeft)
{
    if (!graph) return;

    // Pick two random perimeter nodes — one entry (chain head, source) and
    // one exit (chain tail, sink) — from the lists buildInitialGraph()
    // produced for this grid. For an R×C grid there are 2*(R+C) entries
    // and the same number of exits.
    const QVector<int>& entries = m_graphInfo.entryNodes;
    const QVector<int>& exits   = m_graphInfo.exitNodes;
    if (entries.isEmpty() || exits.isEmpty()) return;

    GraphNode* start = nullptr;
    GraphNode* end   = nullptr;
    std::vector<GraphNode*> path;
    for (int attempt = 0; attempt < 24; ++attempt) {
        int si = QRandomGenerator::global()->bounded(entries.size());
        int ei = QRandomGenerator::global()->bounded(exits.size());
        start = graph->getNodeByID(entries[si]);
        end   = graph->getNodeByID(exits[ei]);
        if (!start || !end || start == end) continue;
        path = graph->shortestPath(start, end);
        if (path.size() >= 2) break;
        path.clear();
    }
    if (path.size() < 2) {
        qWarning() << "🚧 Dijkstra: no route found after retries";
        return;
    }

    // Initial heading: dominant axis of the first segment.
    QPointF p0 = path[0]->getPosition();
    QPointF p1 = path[1]->getPosition();
    qreal dx = p1.x() - p0.x();
    qreal dy = p1.y() - p0.y();
    int initialDir;
    if (qAbs(dx) >= qAbs(dy)) initialDir = (dx > 0) ? 1 : 3;
    else                       initialDir = (dy > 0) ? 2 : 0;

    QString prefix = emergency ? "EMG" : (turnLeft ? "L" : "Car");
    QString id = prefix + QString::number(m_carCounter++);
    Node*    n = new Node(id, emergency, turnLeft);
    CarItem* car = new CarItem(n, initialDir, 0);

    // Convert the path to scene-coord waypoints + parallel graph node IDs.
    // pathNodeIds lets updatePathCarStopFlags() recognize approach nodes
    // without needing a reverse-lookup from position to node.
    // Cursor=1 means "start at waypoint 0, drive toward waypoint 1 next."
    car->pathWaypoints.reserve(static_cast<int>(path.size()));
    car->pathNodeIds.reserve(static_cast<int>(path.size()));
    for (GraphNode* gn : path) {
        car->pathWaypoints.append(gn->getPosition());
        car->pathNodeIds.append(gn->getID());
    }

    // Shift every waypoint to the lane center its driver should be in
    // (turn lane for left turners, outer lane for right turners, inner
    // lane for straight). This shapes the whole route into proper lanes
    // so all three are utilized instead of every car driving on the
    // graph-node centerline.
    car->applyLaneOffsets();

    // ── Anti-overlap on spawn ────────────────────────────────────────
    // Multiple cars spawned at the same entry node within a few ticks
    // would all start at the lane-shifted pathWaypoints[0], producing a
    // visible pile-up. Project every existing car onto our outgoing
    // direction vector at the spawn point; if any sit in the same lane
    // and within ~50 px ahead along the route, push pathWaypoints[0]
    // back along the reverse-direction so we stack with at least one
    // CAR_LEN + GAP between centers.
    if (car->pathWaypoints.size() >= 2) {
        QPointF p0 = car->pathWaypoints[0];
        QPointF p1 = car->pathWaypoints[1];
        qreal vx = p1.x() - p0.x();
        qreal vy = p1.y() - p0.y();
        qreal vlen = qSqrt(vx * vx + vy * vy);
        if (vlen > 0.01) {
            vx /= vlen; vy /= vlen;            // unit fwd
            qreal pushback = 0.0;
            const qreal SAME_LANE_TOL = 12.0;  // perpendicular tolerance
            const qreal MIN_GAP       = 32.0;  // center-to-center
            for (int i = 0; i < m_numIntersections; i++) {
                for (CarItem* other : cars[i]) {
                    if (other == car) continue;
                    QPointF op = other->sceneCenter();
                    qreal dx = op.x() - p0.x();
                    qreal dy = op.y() - p0.y();
                    qreal along   = dx * vx + dy * vy;        // signed along fwd
                    qreal lateral = qAbs(-dy * vx + dx * vy); // perpendicular
                    if (lateral > SAME_LANE_TOL) continue;
                    if (along < -MIN_GAP) continue;           // already behind us
                    if (along >  200.0)   continue;           // too far ahead
                    qreal needed = along + MIN_GAP;
                    if (needed > pushback) pushback = needed;
                }
            }
            if (pushback > 0.0) {
                car->pathWaypoints[0] = QPointF(p0.x() - vx * pushback,
                                                p0.y() - vy * pushback);
            }
        }
    }

    car->pathCursor = 1;
    car->setSceneCenter(car->pathWaypoints[0]);

    // Tag the car with the tile that "owns" the start node, looked up
    // from the GraphInfo map populated by buildInitialGraph. Used only
    // for the HUD per-intersection count; cars wandering across tiles
    // stay in their owner's list regardless of where they actually are.
    int ownerIdx = 0;
    auto ownerIt = m_graphInfo.entryToTile.find(start->getID());
    if (ownerIt != m_graphInfo.entryToTile.end() &&
        ownerIt.value() >= 0 && ownerIt.value() < m_numIntersections)
    {
        ownerIdx = ownerIt.value();
        car->intersectionId = ownerIdx;
        car->originX        = originX[ownerIdx];
        car->originY        = originY[ownerIdx];
    }

    scene->addItem(car);
    cars[ownerIdx].append(car);

    qDebug() << (emergency ? "🚨" : (turnLeft ? "↰" : "🚗")) << id
             << "Dijkstra route:" << start->getID() << "→" << end->getID()
             << "(" << static_cast<int>(path.size()) << "nodes)";
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
    for (int i = 0; i < m_numIntersections; i++) total += cars[i].size();

    QString perInt;
    for (int i = 0; i < m_numIntersections; i++)
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
        "<font color='#143cb4'>■</font> car &nbsp;"
        "<font color='#ff6060'>■</font> emergency &nbsp;"
        "<font color='#ffcc00'>(blinker flashes when turning)</font><br>";

    QString carCount = "<font color='#cccccc'>Cars: " + QString::number(total)
                     + " &nbsp;(" + perInt + ")</font><br>";
    QString selected = QString("<font color='#88ff88'>Selected: %1</font><br>")
                          .arg(tileLabel(m_selectedIntersection));

    QString graphLine = QString("<font color='#cccccc'>Graph overlay: generated rows×cols graph. Hospitals: %1. Use 🚑 Smart Ambulance button.</font><br>")
                            .arg(static_cast<int>(m_hospitalManager.hospitals().size()));

    m_hud->setHtml(carCount + selected + legend + graphLine + emergencyLine + turnLine + controls);
}


void IntersectionWindow::openEmergencyDialog()
{
    if (!graph) {
        QMessageBox::warning(this, QStringLiteral("Smart Ambulance"),
                             QStringLiteral("The road graph is not ready yet."));
        return;
    }
    if (m_graphInfo.entryNodes.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("Smart Ambulance"),
                             QStringLiteral("No perimeter spawn nodes are available."));
        return;
    }

    QDialog dlg(this);
    dlg.setWindowTitle(QStringLiteral("Smart Ambulance Dispatch"));
    dlg.setMinimumWidth(560);
    dlg.setStyleSheet(
        "QDialog { background:#1e1e2e; color:white; }"
        "QLabel { color:white; font-size:13px; }"
        "QComboBox { background:#2c2c3e; color:white; border:1px solid #555; padding:5px; }"
        "QGroupBox { color:#9fd; font-weight:bold; border:1px solid #555; border-radius:6px; margin-top:8px; padding:8px; }"
        "QGroupBox::title { subcontrol-origin: margin; left:8px; padding:0 4px; }"
        "QPushButton { padding:7px 12px; border-radius:6px; font-weight:bold; }"
    );

    QVBoxLayout* mainLay = new QVBoxLayout(&dlg);
    mainLay->setSpacing(10);

    QLabel* intro = new QLabel(QStringLiteral(
        "Choose the emergency type and ambulance spawn point. The system filters hospitals by resources, "
        "runs Dijkstra once from the spawn node, then chooses the hospital with the best treatment score."));
    intro->setWordWrap(true);
    mainLay->addWidget(intro);

    QFormLayout* form = new QFormLayout;
    form->setLabelAlignment(Qt::AlignLeft);

    QComboBox* emergencyCombo = new QComboBox;
    emergencyCombo->addItems(m_hospitalManager.emergencyTypeNames());
    form->addRow(QStringLiteral("Emergency:"), emergencyCombo);

    QComboBox* spawnCombo = new QComboBox;
    for (int nodeId : m_graphInfo.entryNodes) {
        spawnCombo->addItem(QStringLiteral("Node %1  (perimeter ambulance entry)").arg(nodeId), nodeId);
    }
    form->addRow(QStringLiteral("Spawn ambulance at:"), spawnCombo);
    mainLay->addLayout(form);

    QGroupBox* statusBox = new QGroupBox(QStringLiteral("Current hospital resource dataset"));
    QVBoxLayout* statusLay = new QVBoxLayout(statusBox);
    QLabel* statusLabel = new QLabel(m_hospitalManager.statusHtml());
    statusLabel->setTextFormat(Qt::RichText);
    statusLabel->setWordWrap(true);
    statusLay->addWidget(statusLabel);
    mainLay->addWidget(statusBox);

    QGroupBox* resultBox = new QGroupBox(QStringLiteral("Decision result"));
    QVBoxLayout* resultLay = new QVBoxLayout(resultBox);
    QLabel* resultLabel = new QLabel;
    resultLabel->setWordWrap(true);
    resultLabel->setTextFormat(Qt::RichText);
    resultLay->addWidget(resultLabel);
    mainLay->addWidget(resultBox);

    QDialogButtonBox* buttons = new QDialogButtonBox;
    QPushButton* spawnBtn = buttons->addButton(QStringLiteral("Spawn Ambulance"), QDialogButtonBox::AcceptRole);
    QPushButton* cancelBtn = buttons->addButton(QDialogButtonBox::Cancel);
    spawnBtn->setStyleSheet("background:#d64545; color:white;");
    cancelBtn->setStyleSheet("background:#444; color:white;");
    mainLay->addWidget(buttons);

    HospitalDecision latestDecision;

    auto recompute = [&]() {
        const int startNodeId = spawnCombo->currentData().toInt();
        const EmergencyType type = m_hospitalManager.emergencyTypeFromIndex(emergencyCombo->currentIndex());
        latestDecision = m_hospitalManager.chooseBestHospital(graph, startNodeId, type);

        if (latestDecision.found && latestDecision.path.size() >= 2) {
            resultLabel->setText(QString(
                "<font color='#88ff88'><b>%1</b></font><br>"
                "Recommended hospital: <b>%2</b> at graph node <b>%3</b><br>"
                "Route length: <b>%4 nodes</b><br>"
                "Drive cost: <b>%5</b> &nbsp;|&nbsp; Hospital wait: <b>%6 min</b> &nbsp;|&nbsp; Total score: <b>%7</b><br>"
                "<font color='#bbb'>Score = Dijkstra drive cost + hospital wait time + overload penalty.</font>")
                .arg(m_hospitalManager.emergencyTypeName(type))
                .arg(latestDecision.hospitalName)
                .arg(latestDecision.hospitalNodeId)
                .arg(static_cast<int>(latestDecision.path.size()))
                .arg(std::round(latestDecision.driveCost))
                .arg(latestDecision.waitMinutes)
                .arg(std::round(latestDecision.totalScore)));
            spawnBtn->setEnabled(true);
        } else {
            resultLabel->setText(QString("<font color='#ff8888'><b>No hospital ready.</b></font><br><pre>%1</pre>")
                                 .arg(latestDecision.reason.toHtmlEscaped()));
            spawnBtn->setEnabled(false);
        }
    };

    connect(emergencyCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), &dlg, [&](int){ recompute(); });
    connect(spawnCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), &dlg, [&](int){ recompute(); });
    connect(cancelBtn, &QPushButton::clicked, &dlg, &QDialog::reject);
    connect(spawnBtn, &QPushButton::clicked, &dlg, [&]() {
        const int startNodeId = spawnCombo->currentData().toInt();
        const EmergencyType type = m_hospitalManager.emergencyTypeFromIndex(emergencyCombo->currentIndex());
        if (spawnAmbulanceForEmergency(startNodeId, type)) {
            dlg.accept();
        } else {
            QMessageBox::warning(&dlg, QStringLiteral("Smart Ambulance"),
                                 QStringLiteral("Could not spawn the ambulance for this selection."));
        }
    });

    recompute();
    dlg.exec();
}

bool IntersectionWindow::spawnAmbulanceForEmergency(int startNodeId, EmergencyType emergencyType)
{
    HospitalDecision decision = m_hospitalManager.chooseBestHospital(graph, startNodeId, emergencyType);
    if (!decision.found || decision.path.size() < 2) {
        qWarning() << "Smart Ambulance: no valid dispatch" << decision.reason;
        return false;
    }

    spawnCarOnPath(decision.path, true);
    m_hospitalManager.consumeResources(decision.hospitalId, emergencyType);

    qDebug() << "🚑 Smart Ambulance dispatched from node" << startNodeId
             << "to" << decision.hospitalName
             << "hospital node" << decision.hospitalNodeId
             << "for" << m_hospitalManager.emergencyTypeName(emergencyType);

    updateHud();
    return true;
}

// ── Clear ─────────────────────────────────────────────────────────────────
void IntersectionWindow::clearScene()
{
    for (int i = 0; i < m_numIntersections; i++) {
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

    // Clear the GraphInfo lookup tables; buildInitialGraph repopulates
    // them and re-points CarItem::graphInfo at m_graphInfo.
    m_graphInfo.clear();
    CarItem::graphInfo = nullptr;

    // Reset Dijkstra animation state — its visual items are owned by
    // the scene and were already deleted by scene->clear().
    m_dijStage  = DIJ_IDLE;
    m_dijStart  = nullptr;
    m_dijEnd    = nullptr;
    m_dijDist.clear();
    m_dijPrev.clear();
    m_dijSettled.clear();
    while (!m_dijFrontier.empty()) m_dijFrontier.pop();
    m_dijPath.clear();
    m_dijStepCounter = 0;
    m_dijDoneTimer   = 0;
    m_dijHud   = nullptr;
    m_dijHudBg = nullptr;
    m_edgeVisuals.clear();
    m_dijTreeEdgeForNode.clear();

    m_nextIsEmergency  = false;
    m_nextIsTurnLeft   = false;
    m_tickCount        = 0;
    m_carCounter       = 0;
    m_hud              = nullptr;
    m_emergencyButtonProxy = nullptr;
    m_hospitalManager.clear();
}


void IntersectionWindow::restartSimulation()
{
    timer->stop();
    clearScene();
    buildScene();
    buildInitialGraph();
    if (m_manualMode) buildDijkstraHud();
    timer->start(50);
    qDebug() << "🔄 Restarted (" << m_rows << "×" << m_cols
             << "grid =" << m_numIntersections << "intersections)";
}

// ── Key press ─────────────────────────────────────────────────────────────
void IntersectionWindow::keyPressEvent(QKeyEvent* event)
{
    switch (event->key()) {
    case Qt::Key_R: restartSimulation(); break;

    // 1/2/3/4 — pick which intersection subsequent N/E/S/W spawns will go to.
    // 1/2/3/4 keys are HUD-only intersection selectors — clamped to the
    // current grid size so the HUD doesn't show an invalid tile.
    case Qt::Key_1: if (m_numIntersections > 0) m_selectedIntersection = qMin(0, m_numIntersections - 1); updateHud(); break;
    case Qt::Key_2: if (m_numIntersections > 1) m_selectedIntersection = qMin(1, m_numIntersections - 1); updateHud(); break;
    case Qt::Key_3: if (m_numIntersections > 2) m_selectedIntersection = qMin(2, m_numIntersections - 1); updateHud(); break;
    case Qt::Key_4: if (m_numIntersections > 3) m_selectedIntersection = qMin(3, m_numIntersections - 1); updateHud(); break;

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
    for (int i = 0; i < m_numIntersections; i++) {
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
    for (int i = 0; i < m_numIntersections; i++)
        for (CarItem* car : cars[i]) car->animateLateral();
}

// computeEffectiveStops() used to compute per-car effectiveStop along
// each direction's queue. That logic is now handled by
// updatePathCarStopFlags(), which writes a rank-based pathStopPos to each
// blocked path car. We keep the function as a no-op stub so the header
// signature stays valid while every existing call site is removed.
void IntersectionWindow::computeEffectiveStops(int /*idx*/) {}

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

    // (Turn-queue counts and effective stop spacing are now produced by
    //  updatePathCarStopFlags() in one cross-intersection pass, since path
    //  cars belong to one intersection's tile but may be queued at any.)

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

// ── Interactive Dijkstra (manual mode only) ─────────────────────────────

// Recolor a single edge's QGraphicsPathItem based on its visual state.
// The defaults (state == EV_DEFAULT) match buildInitialGraph's faint
// semi-transparent yellow at z=4. EV_TREE highlights an edge that's
// currently the algorithm's best-known incoming route to its endpoint
// (orange, thicker, z=11). EV_PATH highlights an edge that's part of
// the final reconstructed shortest path (cyan, thicker still, z=12).
void IntersectionWindow::setEdgeVisualState(Edge* e, int state)
{
    if (!e) return;
    auto it = m_edgeVisuals.find(e);
    if (it == m_edgeVisuals.end() || !it.value()) return;
    QGraphicsPathItem* vis = it.value();
    QPen p;
    p.setCapStyle(Qt::RoundCap);
    p.setJoinStyle(Qt::RoundJoin);
    qreal z = 4;
    switch (state) {
    case EV_TREE:
        p.setColor(QColor(255, 170, 60, 240));
        p.setWidthF(5.0);
        z = 11;
        break;
    case EV_PATH:
        p.setColor(QColor(0, 200, 230, 255));
        p.setWidthF(7.0);
        z = 12;
        break;
    case EV_DEFAULT:
    default:
        p.setColor(QColor(255, 245, 80, 60));
        p.setWidthF(4.0);
        z = 4;
        break;
    }
    vis->setPen(p);
    vis->setZValue(z);
}

// Restore every edge to its default look (used when resetting between
// searches). Also clears the per-node tree-edge tracking hash.
void IntersectionWindow::resetAllEdgeVisuals()
{
    for (auto it = m_edgeVisuals.constBegin(); it != m_edgeVisuals.constEnd(); ++it) {
        setEdgeVisualState(it.key(), EV_DEFAULT);
    }
    m_dijTreeEdgeForNode.clear();
}

// Linear-scan u's outgoing edges for the one ending at v. Each node has
// at most a handful of outgoing edges, so this stays cheap.
Edge* IntersectionWindow::findEdge(GraphNode* u, GraphNode* v) const
{
    if (!u || !v) return nullptr;
    for (Edge* e : u->getOutgoingEdges()) {
        if (e && e->getEndNode() == v) return e;
    }
    return nullptr;
}

// Small status panel at the top-right corner of the scene, telling the
// user what the algorithm is doing right now: which click to make next,
// or that the algorithm is animating, or that a path was found.
void IntersectionWindow::buildDijkstraHud()
{
    if (!scene) return;
    const qreal w = 360.0;
    const qreal h = 160.0;
    const qreal x = m_sceneWidth - w - 12.0;
    const qreal y = 12.0;

    m_dijHudBg = scene->addRect(x, y, w, h,
                                QPen(QColor(255, 255, 255, 60), 1.5),
                                QBrush(QColor(0, 0, 0, 210)));
    m_dijHudBg->setZValue(29);

    m_dijHud = scene->addText("");
    m_dijHud->setDefaultTextColor(Qt::white);
    m_dijHud->setFont(QFont("Helvetica", 14));
    m_dijHud->setTextWidth(w - 24);
    m_dijHud->setPos(x + 12, y + 8);
    m_dijHud->setZValue(30);

    updateDijkstraHud();
}

void IntersectionWindow::updateDijkstraHud()
{
    if (!m_dijHud) return;
    QString stageText;
    switch (m_dijStage) {
    case DIJ_IDLE:
        stageText = "<font color='#9fd'><b>Dijkstra demo</b></font><br>"
                    "Click any graph node to set the <b>start</b>.";
        break;
    case DIJ_SELECT_END:
        stageText = QString("<font color='#9fd'><b>Dijkstra demo</b></font><br>"
                            "Start = node <b>%1</b>. Click another node for the <b>end</b>.")
                    .arg(m_dijStart ? m_dijStart->getID() : -1);
        break;
    case DIJ_ANIMATE:
        stageText = QString("<font color='#9fd'><b>Dijkstra demo</b></font><br>"
                            "Running… settled <b>%1</b> nodes, frontier <b>%2</b>.")
                    .arg(m_dijSettled.size())
                    .arg(static_cast<int>(m_dijFrontier.size()));
        break;
    case DIJ_PATH_SHOWN:
        if (m_dijPath.empty()) {
            // Frontier exhausted before reaching the end — directed graph
            // means the destination is genuinely unreachable from the
            // start. Common cause: start is a perimeter EXIT (no outgoing
            // edges), or end is a perimeter ENTRY (no incoming edges).
            stageText = QString(
                "<font color='#f88'><b>No path found</b></font><br>"
                "Node <b>%1</b> can't reach node <b>%2</b>. "
                "<font size='-1' color='#aaa'>Each road chain is one-way; "
                "perimeter entries → perimeter exits always work.</font>")
                .arg(m_dijStart ? m_dijStart->getID() : -1)
                .arg(m_dijEnd   ? m_dijEnd->getID()   : -1);
        } else {
            stageText = QString("<font color='#9fd'><b>Path found</b></font><br>"
                                "Length <b>%1</b> nodes. Spawning car…")
                        .arg(static_cast<int>(m_dijPath.size()));
        }
        break;
    case DIJ_CAR_DRIVING:
        if (m_dijPath.empty()) {
            stageText = "<font color='#f88'><b>No car spawned</b></font><br>"
                        "Click any node to try again.";
        } else {
            stageText = "<font color='#9fd'><b>Car en route</b></font><br>"
                        "Click any node to start a new search.";
        }
        break;
    }
    QString legend =
        "<br><font size='-1' color='#bbb'>"
        "<font color='#4cdc5a'>●</font> start &nbsp;"
        "<font color='#f04646'>●</font> end &nbsp;"
        "<font color='#ffaa3c'>━</font> tree edge &nbsp;"
        "<font color='#00c8e6'>━</font> final path"
        "</font>";
    m_dijHud->setHtml(stageText + legend);
}

// Click on any graph node (in manual mode) drives the state machine. The
// first click in IDLE picks the start; the second picks the end and kicks
// off the animated algorithm. Any click while a car is already driving
// resets and starts a new selection.
void IntersectionWindow::handleNodeClick(GraphNode* node)
{
    if (!node) return;

    // Mid-algorithm or mid-pause clicks are ignored — let the animation
    // play out so it stays legible. Once the car is driving the user can
    // restart freely.
    if (m_dijStage == DIJ_ANIMATE || m_dijStage == DIJ_PATH_SHOWN) return;

    if (m_dijStage == DIJ_CAR_DRIVING) {
        resetDijkstraSelection();
        // fall through to picking a new start
    }

    if (m_dijStage == DIJ_IDLE) {
        m_dijStart = node;
        node->setVisualState(GraphNode::VS_START);
        m_dijStage = DIJ_SELECT_END;
        updateDijkstraHud();
        return;
    }

    if (m_dijStage == DIJ_SELECT_END) {
        if (node == m_dijStart) return;   // ignore same-node click
        m_dijEnd = node;
        node->setVisualState(GraphNode::VS_END);
        startDijkstraAnimation();
        return;
    }
}

// Override mousePressEvent to detect clicks on graph nodes in manual mode.
// Delegates to QGraphicsView for anything that isn't a node-click so
// scrolling / selection rectangles still work if Qt's default flags
// happen to be active.
void IntersectionWindow::mousePressEvent(QMouseEvent* event)
{
    if (m_manualMode && event->button() == Qt::LeftButton && scene) {
        QPointF scenePos = mapToScene(event->pos());
        // Check every item at this point — the top-most GraphNode wins.
        const QList<QGraphicsItem*> items = scene->items(scenePos);
        for (QGraphicsItem* it : items) {
            if (GraphNode* node = dynamic_cast<GraphNode*>(it)) {
                handleNodeClick(node);
                event->accept();
                return;
            }
        }
    }
    QGraphicsView::mousePressEvent(event);
}

// Initialize the Dijkstra runtime state with `m_dijStart` seeded at dist=0.
// The step pump (tickDijkstraAnimation → stepDijkstra) takes over from here.
void IntersectionWindow::startDijkstraAnimation()
{
    m_dijDist.clear();
    m_dijPrev.clear();
    m_dijSettled.clear();
    while (!m_dijFrontier.empty()) m_dijFrontier.pop();
    m_dijPath.clear();

    if (!m_dijStart) { resetDijkstraSelection(); return; }
    m_dijDist[m_dijStart->getID()] = 0.0;
    m_dijFrontier.push({ 0.0, m_dijStart->getID() });

    m_dijStage = DIJ_ANIMATE;
    m_dijStepCounter = 0;   // step on the very next tick
    updateDijkstraHud();
}

// One Dijkstra step: pop the smallest-dist frontier entry, mark it
// settled, relax outgoing edges, push improved neighbors. Settled and
// frontier nodes get distinct colors. If the popped node is the end,
// the animation finishes with success; if the frontier empties first,
// there's no path and we finish with failure.
void IntersectionWindow::stepDijkstra()
{
    if (m_dijFrontier.empty()) {
        finishDijkstraAnimation(false);
        return;
    }

    // Discard any stale entries (Dijkstra with lazy deletion).
    DijFrontierEntry top = m_dijFrontier.top();
    m_dijFrontier.pop();
    while (m_dijSettled.contains(top.nodeId)) {
        if (m_dijFrontier.empty()) {
            finishDijkstraAnimation(false);
            return;
        }
        top = m_dijFrontier.top();
        m_dijFrontier.pop();
    }

    qreal d = top.dist;
    int   uId = top.nodeId;
    GraphNode* u = graph ? graph->getNodeByID(uId) : nullptr;
    if (!u) { finishDijkstraAnimation(false); return; }

    m_dijSettled.insert(uId);

    if (u == m_dijEnd) {
        finishDijkstraAnimation(true);
        return;
    }

    // Relax outgoing edges. Whenever a relaxation improves v's distance,
    // edge (u, v) becomes v's "tree edge" (best-known incoming route).
    // We highlight that edge as EV_TREE and, if v previously had a
    // different tree edge, reset that old one back to EV_DEFAULT so the
    // shortest-path-tree visualization stays correct as it grows.
    for (Edge* e : u->getOutgoingEdges()) {
        if (!e) continue;
        GraphNode* v = e->getEndNode();
        if (!v) continue;
        if (m_dijSettled.contains(v->getID())) continue;

        qreal alt = d + e->getWeight();
        auto it = m_dijDist.find(v->getID());
        if (it == m_dijDist.end() || alt < it.value()) {
            m_dijDist[v->getID()] = alt;
            m_dijPrev[v->getID()] = u;
            m_dijFrontier.push({ alt, v->getID() });

            auto oldEdgeIt = m_dijTreeEdgeForNode.find(v->getID());
            if (oldEdgeIt != m_dijTreeEdgeForNode.end() && oldEdgeIt.value() != e) {
                setEdgeVisualState(oldEdgeIt.value(), EV_DEFAULT);
            }
            m_dijTreeEdgeForNode[v->getID()] = e;
            setEdgeVisualState(e, EV_TREE);
        }
    }
}

// After the algorithm has either reached the end or exhausted the
// frontier, reconstruct the path (if any) and color it as VS_PATH. A
// short pause (DIJ_PATH_SHOWN) lets the user see the result, then a car
// is spawned that drives the same path.
void IntersectionWindow::finishDijkstraAnimation(bool success)
{
    m_dijPath.clear();
    if (success && m_dijEnd) {
        GraphNode* cur = m_dijEnd;
        while (cur) {
            m_dijPath.insert(m_dijPath.begin(), cur);
            if (cur == m_dijStart) break;
            auto it = m_dijPrev.find(cur->getID());
            cur = (it != m_dijPrev.end()) ? it.value() : nullptr;
        }
        if (m_dijPath.empty() || m_dijPath.front() != m_dijStart) {
            success = false;
            m_dijPath.clear();
        }
    }
    if (success) {
        // Color the actual edges along the path cyan (overwriting the
        // orange tree-edge coloring that was applied during exploration
        // for these particular edges).
        for (std::size_t i = 0; i + 1 < m_dijPath.size(); i++) {
            Edge* e = findEdge(m_dijPath[i], m_dijPath[i + 1]);
            if (e) setEdgeVisualState(e, EV_PATH);
        }
    }
    m_dijStage = DIJ_PATH_SHOWN;
    m_dijDoneTimer = success ? 40 : 80;   // ~2s success, ~4s on failure
    updateDijkstraHud();
}

// Spawn a single CarItem whose pathWaypoints are the scene positions of
// the chosen graph nodes — same machinery as the random-route spawn,
// just with a fixed (start, end) handed in by the user.
void IntersectionWindow::spawnCarOnPath(const std::vector<GraphNode*>& nodes, bool emergency)
{
    if (nodes.size() < 2) return;

    QPointF p0 = nodes[0]->getPosition();
    QPointF p1 = nodes[1]->getPosition();
    qreal dx = p1.x() - p0.x();
    qreal dy = p1.y() - p0.y();
    int initialDir;
    if (qAbs(dx) >= qAbs(dy)) initialDir = (dx > 0) ? 1 : 3;
    else                      initialDir = (dy > 0) ? 2 : 0;

    QString prefix = emergency ? "EMG" : "Car";
    QString id = prefix + QString::number(m_carCounter++);
    Node*    n   = new Node(id, emergency, false);
    CarItem* car = new CarItem(n, initialDir, 0);

    car->pathWaypoints.reserve(static_cast<int>(nodes.size()));
    car->pathNodeIds.reserve(static_cast<int>(nodes.size()));
    for (GraphNode* gn : nodes) {
        car->pathWaypoints.append(gn->getPosition());
        car->pathNodeIds.append(gn->getID());
    }
    car->applyLaneOffsets();
    car->pathCursor = 1;
    car->setSceneCenter(car->pathWaypoints[0]);

    // Tile bookkeeping for the HUD count.
    int ownerIdx = 0;
    auto ownerIt = m_graphInfo.entryToTile.find(nodes[0]->getID());
    if (ownerIt != m_graphInfo.entryToTile.end() &&
        ownerIt.value() >= 0 && ownerIt.value() < m_numIntersections)
    {
        ownerIdx = ownerIt.value();
    }
    car->intersectionId = ownerIdx;
    car->originX        = originX[ownerIdx];
    car->originY        = originY[ownerIdx];

    scene->addItem(car);
    cars[ownerIdx].append(car);

    qDebug() << "🛣️ Dijkstra-spawned" << id
             << "nodes:" << nodes.front()->getID() << "→" << nodes.back()->getID()
             << "(" << static_cast<int>(nodes.size()) << "waypoints)";
}

// Wipe the algorithm state and reset all visual states back to default.
// Called when the user clicks while a car from the previous run is
// still driving — they get a fresh slate for the next selection.
void IntersectionWindow::resetDijkstraSelection()
{
    // Repaint every graph node and every edge back to default. The only
    // node colors that survive after reset would be START/END but those
    // get cleared here too because m_dijStart and m_dijEnd are nulled
    // immediately after.
    if (graph) {
        for (GraphNode* n : graph->getNodes()) n->setVisualState(GraphNode::VS_DEFAULT);
    }
    resetAllEdgeVisuals();
    m_dijStage   = DIJ_IDLE;
    m_dijStart   = nullptr;
    m_dijEnd     = nullptr;
    m_dijPath.clear();
    m_dijDist.clear();
    m_dijPrev.clear();
    m_dijSettled.clear();
    while (!m_dijFrontier.empty()) m_dijFrontier.pop();
    m_dijStepCounter = 0;
    m_dijDoneTimer   = 0;
    updateDijkstraHud();
}

// Pump the Dijkstra animation forward once per simulation tick.
//   - DIJ_ANIMATE: step the algorithm every STEP_INTERVAL ticks.
//   - DIJ_PATH_SHOWN: countdown m_dijDoneTimer, then spawn a car on the
//     final path and move to DIJ_CAR_DRIVING.
// Other stages are passive — they just wait for clicks.
void IntersectionWindow::tickDijkstraAnimation()
{
    if (!m_manualMode) return;

    const int STEP_INTERVAL = 6;   // ticks between algorithm steps

    if (m_dijStage == DIJ_ANIMATE) {
        if (m_dijStepCounter > 0) {
            m_dijStepCounter--;
        } else {
            stepDijkstra();
            m_dijStepCounter = STEP_INTERVAL;
            updateDijkstraHud();
        }
    } else if (m_dijStage == DIJ_PATH_SHOWN) {
        if (m_dijDoneTimer > 0) {
            m_dijDoneTimer--;
        } else {
            if (!m_dijPath.empty()) spawnCarOnPath(m_dijPath, false);
            m_dijStage = DIJ_CAR_DRIVING;
            updateDijkstraHud();
        }
    }
}

// ── Per-tick global update ────────────────────────────────────────────────
void IntersectionWindow::updateSimulation()
{
    m_tickCount++;
    if (!m_manualMode)
        processSpawnSchedule();

    animateAllLateral();
    for (int i = 0; i < m_numIntersections; i++)
        for (CarItem* car : cars[i]) car->updateBlinker();

    // Emergency preemption FIRST: forces lights green for any emergency
    // approaching/crossing each intersection, and clears the override on
    // intersections without one. Has to run before updatePathCarStopFlags
    // so normal cars at the same intersection see the forced-red lights
    // (and stop), and before updateOneIntersection so advanceLights()
    // sees the override and freezes its phase cycle while it's active.
    propagateEmergencyOverrides();

    // Interactive Dijkstra demo: only does work in manual mode while
    // animating or pausing before the car spawn. Cheap no-op otherwise.
    tickDijkstraAnimation();

    // Refresh path-car red-light stop flags BEFORE the per-intersection
    // updates that call movePath() — otherwise a car arriving at an
    // approach node this tick would see last tick's light state.
    updatePathCarStopFlags();

    for (int i = 0; i < m_numIntersections; i++) updateOneIntersection(i);

    updateLightVisuals();
    updateHud();
}



