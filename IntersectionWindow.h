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

class IntersectionWindow : public QGraphicsView {
    Q_OBJECT

private:
    QGraphicsScene*    scene;
    QTimer*            timer;
    TrafficController  controller;
    QList<CarItem*>    cars;
    DirectionalLight*  lightIndicators[4];

    bool m_manualMode;        // true = manual spawn mode
    bool m_nextIsEmergency;   // true = V was pressed, next spawn is emergency
    int  m_carCounter;
    bool m_emergencyWaiting;  // true = waiting for intersection to clear before releasing emergency
    int  m_splittingDir;      // direction currently splitting (-1 = none)
    bool m_splitComplete;     // true once all cars have finished shifting aside
    CarItem* m_releasedEmergency;  // track released emergency car until it exits scene
    int  m_tickCount;             // simulation tick counter for scripted spawns
    QGraphicsTextItem* m_hud;

    void buildScene();
    void buildSimulationCars();
    void clearScene();
    void spawnCar(int dir, bool emergency);           // picks a random lane
    void spawnCarAt(int dir, qreal x, qreal y, bool emergency); // explicit position
    void updateHud();
    void keyPressEvent(QKeyEvent* event) override;
    void updateLightVisuals();
    void computeEffectiveStops();
    bool isIntersectionClear() const;
    void startLaneSplit(int dir);
    void endLaneSplit(int dir);
    bool isLaneSplitComplete(int dir) const;
    void animateAllLateral();
    void processSpawnSchedule();

public:
    explicit IntersectionWindow(bool manualMode);

public slots:
    void updateSimulation();
    void restartSimulation();
};

#endif
