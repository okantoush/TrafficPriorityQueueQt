#ifndef INTERSECTIONWINDOW_H
#define INTERSECTIONWINDOW_H

#include <QGraphicsView>
#include <QKeyEvent>
#include <QGraphicsScene>
#include <QGraphicsRectItem>
#include <QGraphicsTextItem>
#include <QTimer>
#include "TrafficController.h"
#include "CarItem.h"
#include "DirectionalLight.h"
#include "graphmanager.h"

class IntersectionWindow : public QGraphicsView {
    Q_OBJECT

private:
    // ── Multi-intersection layout ───────────────────────────────────────
    // 4 intersections in a 2×2 grid, each occupying a self-contained
    // 600×600 tile of the scene. Cars spawned at one intersection stay
    // within that intersection's tile and exit at its tile edge.
    static constexpr int NUM_INT = 4;
    static const qreal originX[NUM_INT];
    static const qreal originY[NUM_INT];

    QGraphicsScene*    scene;
    QTimer*            timer;

    // Per-intersection state (parallel arrays).
    TrafficController  controllers[NUM_INT];
    QList<CarItem*>    cars[NUM_INT];
    DirectionalLight*  lightIndicators[NUM_INT][4];
    DirectionalLight*  turnLightIndicators[NUM_INT][4];
    int                m_splittingDir[NUM_INT];
    bool               m_splitComplete[NUM_INT];
    bool               m_emergencyWaiting[NUM_INT];
    CarItem*           m_releasedEmergency[NUM_INT];

    bool m_manualMode;
    bool m_nextIsEmergency;
    bool m_nextIsTurnLeft;
    int  m_carCounter;
    int  m_selectedIntersection;   // 0..3 — current target for keyboard spawns
    int  m_tickCount;
    QGraphicsTextItem* m_hud;

    // Visual graph/node overlay
    GraphManager*      graph;

    void buildScene();
    void buildIntersectionTile(int idx);   // draws one tile's roads/lights
    void buildSimulationCars();
    void buildInitialGraph();
    void clearScene();
    void spawnCarFromGraphRandomRoute(bool emergency = false, bool turnLeft = false);
    void spawnCarOnGraphEntry(GraphNode* entry, int destNodeId,
                              bool emergency = false, bool turnLeft = false);
    static bool mapGraphEntryNodeIdToTile(int entryNodeId, int& outIdx, int& outDir);
    bool isTurnLightGreen(int idx, int dir) const;
    void updateHud();
    void keyPressEvent(QKeyEvent* event) override;
    void updateLightVisuals();
    void computeEffectiveStops(int idx);
    bool isIntersectionClear(int idx) const;
    void startLaneSplit(int idx, int dir);
    void endLaneSplit(int idx, int dir);
    bool isLaneSplitComplete(int idx, int dir) const;
    void animateAllLateral();
    void processSpawnSchedule();
    void updateOneIntersection(int idx);

public:
    explicit IntersectionWindow(bool manualMode);
    ~IntersectionWindow() override;

public slots:
    void updateSimulation();
    void restartSimulation();
};

#endif
