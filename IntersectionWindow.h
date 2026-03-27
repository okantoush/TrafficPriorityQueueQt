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
    QGraphicsTextItem* m_hud;

    void buildScene();
    void buildSimulationCars();
    void clearScene();
    void spawnCar(int dir, bool emergency);           // picks a random lane
    void spawnCarAt(int dir, qreal x, qreal y, bool emergency); // explicit position
    void updateHud();
    void keyPressEvent(QKeyEvent* event) override;
    void updateLightVisuals();

public:
    explicit IntersectionWindow(bool manualMode);

public slots:
    void updateSimulation();
    void restartSimulation();
};

#endif
