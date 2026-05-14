#ifndef INTERSECTIONWINDOW_H
#define INTERSECTIONWINDOW_H

#include <QGraphicsView>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QGraphicsScene>
#include <QGraphicsRectItem>
#include <QGraphicsTextItem>
#include <QTimer>
#include <QVector>
#include <QHash>
#include <QSet>
#include <queue>
#include <vector>
#include "TrafficController.h"
#include "CarItem.h"
#include "DirectionalLight.h"
#include "graphmanager.h"
#include "graphinfo.h"
#include "HospitalManager.h"

class QPushButton;
class QGraphicsProxyWidget;

class IntersectionWindow : public QGraphicsView {
    Q_OBJECT

private:
    // ── Grid layout (dynamic) ───────────────────────────────────────────
    // Constructor takes (rows, cols) and computes everything below from
    // them at runtime. Each tile is a self-contained 600×600 cell of the
    // scene; the scene is cols*600 wide × (rows*600 + 400) tall with
    // 200-px stubs above the top row and below the bottom row.
    int                m_rows;
    int                m_cols;
    int                m_numIntersections;   // = rows * cols
    qreal              m_sceneWidth;
    qreal              m_sceneHeight;
    QVector<qreal>     originX;              // [intId] tile top-left x
    QVector<qreal>     originY;              // [intId] tile top-left y

    QGraphicsScene*    scene;
    QTimer*            timer;

    // Per-intersection state (parallel arrays of size m_numIntersections).
    QVector<TrafficController>           controllers;
    QVector<QList<CarItem*>>             cars;
    QVector<QVector<DirectionalLight*>>  lightIndicators;        // [intId][dir]
    QVector<QVector<DirectionalLight*>>  turnLightIndicators;    // [intId][dir]
    QVector<int>                         m_splittingDir;
    QVector<bool>                        m_splitComplete;
    QVector<bool>                        m_emergencyWaiting;
    QVector<CarItem*>                    m_releasedEmergency;

    bool m_manualMode;
    bool m_nextIsEmergency;
    bool m_nextIsTurnLeft;
    int  m_carCounter;
    int  m_selectedIntersection;   // 0..numInt-1 — current target for HUD
    int  m_tickCount;
    QGraphicsTextItem* m_hud;
    QGraphicsProxyWidget* m_emergencyButtonProxy;
    HospitalManager m_hospitalManager;

    // ── Interactive Dijkstra (manual mode) ───────────────────────────
    // Click two graph nodes on the map to set the start and end; the
    // algorithm then animates step-by-step (one node per ~6 ticks) and
    // once it terminates a car is spawned that drives the found path.
    enum DijkstraStage {
        DIJ_IDLE,          // waiting for the first click
        DIJ_SELECT_END,    // start chosen, waiting for the end click
        DIJ_ANIMATE,       // stepping through the algorithm
        DIJ_PATH_SHOWN,    // path highlighted, brief pause before spawning
        DIJ_CAR_DRIVING    // car spawned and driving; next click resets
    };

    struct DijFrontierEntry {
        qreal dist;
        int   nodeId;
        // Min-heap order — smaller dist pops first.
        bool operator<(const DijFrontierEntry& o) const { return dist > o.dist; }
    };

    DijkstraStage          m_dijStage;
    class GraphNode*       m_dijStart;
    class GraphNode*       m_dijEnd;
    QHash<int, qreal>      m_dijDist;
    QHash<int, GraphNode*> m_dijPrev;
    QSet<int>              m_dijSettled;
    std::priority_queue<DijFrontierEntry> m_dijFrontier;
    std::vector<GraphNode*> m_dijPath;
    int m_dijStepCounter;          // ticks until next algorithm step
    int m_dijDoneTimer;            // ticks of pause at DIJ_PATH_SHOWN
    QGraphicsTextItem*     m_dijHud;       // small status panel at top-right
    QGraphicsRectItem*     m_dijHudBg;     // its dark background

    // Every graph edge has a corresponding QGraphicsPathItem drawn over
    // the road in buildInitialGraph. We keep the pointer so the Dijkstra
    // animation can recolor edges directly. m_dijTreeEdgeForNode tracks
    // which edge is currently "the best-known incoming path" for each
    // node, so when a relaxation discovers a shorter route the previous
    // tree edge can be uncolored before the new one is highlighted.
    QHash<class Edge*, QGraphicsPathItem*> m_edgeVisuals;
    QHash<int, class Edge*>                m_dijTreeEdgeForNode;

    // Road graph + lookup tables. m_graphInfo is shared with CarItem via
    // its static graphInfo pointer.
    GraphManager*      graph;
    GraphInfo          m_graphInfo;

    void buildScene();
    void buildIntersectionTile(int idx);   // draws one tile's roads/lights
    void buildSimulationCars();
    void buildInitialGraph();              // generates the rows × cols graph
    void buildEmergencyButton();            // small Smart Ambulance button, shown in both modes
    void clearScene();
    void spawnCarFromGraphRandomRoute(bool emergency = false, bool turnLeft = false);

    void updatePathCarStopFlags();
    // Once per tick: for each intersection, scan all path cars across
    // every tile for any emergency that's approaching or currently
    // crossing it. If found, force that intersection's lights green for
    // the emergency's direction (turn-light if it's a left turn, straight
    // light otherwise). Otherwise clear the override and let the normal
    // phase cycle resume.
    void propagateEmergencyOverrides();

    // Interactive Dijkstra (manual mode only).
    void buildDijkstraHud();              // small status panel at top-right
    void updateDijkstraHud();
    void handleNodeClick(class GraphNode* node);
    void startDijkstraAnimation();        // initialize frontier with m_dijStart
    void stepDijkstra();                  // process one node
    void finishDijkstraAnimation(bool success);
    void spawnCarOnPath(const std::vector<GraphNode*>& nodes, bool emergency);
    void openEmergencyDialog();
    bool spawnAmbulanceForEmergency(int startNodeId, EmergencyType emergencyType);
    void resetDijkstraSelection();        // back to DIJ_IDLE, clear node colors
    void tickDijkstraAnimation();         // called once per simulation tick

    // Edge-visual coloring helpers used by the Dijkstra animation. State
    // values: 0 = default (faint yellow), 1 = tree edge (orange, drawn
    // while the algorithm is exploring), 2 = final path edge (cyan,
    // drawn after the algorithm terminates).
    enum EdgeVisualState { EV_DEFAULT = 0, EV_TREE = 1, EV_PATH = 2 };
    void setEdgeVisualState(class Edge* e, int state);
    void resetAllEdgeVisuals();
    // Find the edge object connecting `u → v` by linear scan of u's
    // outgoing edges. Returns nullptr if no such edge exists.
    class Edge* findEdge(class GraphNode* u, class GraphNode* v) const;
    bool isTurnLightGreen(int idx, int dir) const;
    void updateHud();
    void keyPressEvent(QKeyEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void updateLightVisuals();
    void computeEffectiveStops(int idx);
    bool isIntersectionClear(int idx) const;
    void startLaneSplit(int idx, int dir);
    void endLaneSplit(int idx, int dir);
    bool isLaneSplitComplete(int idx, int dir) const;
    void animateAllLateral();
    void processSpawnSchedule();
    void updateOneIntersection(int idx);

    // Human-readable label for a tile, used by the HUD selector.
    QString tileLabel(int idx) const;

public:
    // rows, cols default to the 2×2 layout that the simulation has used
    // historically. The mode dialog in main.cpp lets the user override
    // them at startup.
    explicit IntersectionWindow(bool manualMode, int rows = 2, int cols = 2);
    ~IntersectionWindow() override;

public slots:
    void updateSimulation();
    void restartSimulation();
};

#endif
