#ifndef TRAFFICCONTROLLER_H
#define TRAFFICCONTROLLER_H

#include "Lane.h"
#include "PriorityQueue.h"
#include "TrafficLight.h"
#include <QDebug>

class TrafficController {
private:
    Lane         lanes[4];
    TrafficLight lights[4];
    PriorityQueue emergencyQueue; // FIFO priority queue — first-in first-out for equal priority

    int  currentPhase;
    int  ticksRemaining;

    bool phaseHasCars(int phase) const {
        if (phase == 0) return !lanes[0].isEmpty() || !lanes[2].isEmpty();
        else            return !lanes[1].isEmpty() || !lanes[3].isEmpty();
    }

    bool anyLaneHasCars() const {
        for (int i = 0; i < 4; i++)
            if (!lanes[i].isEmpty()) return true;
        return false;
    }

    int calculateGreenTicks() {
        int maxSize = (currentPhase == 0)
        ? qMax(lanes[0].getSize(), lanes[2].getSize())
        : qMax(lanes[1].getSize(), lanes[3].getSize());
        if (maxSize == 0) return 20;
        if (maxSize < 3)  return 60;
        if (maxSize < 6)  return 100;
        return 140;
    }

    void activatePhase(int phase) {
        currentPhase = phase;
        for (int i = 0; i < 4; i++) lights[i].state = RED;
        if (phase == 0) { lights[0].state = GREEN; lights[2].state = GREEN; }
        else            { lights[1].state = GREEN; lights[3].state = GREEN; }
        ticksRemaining = calculateGreenTicks();
        qDebug() << "🟢 Phase" << phase
                 << (phase == 0 ? ": N+S GREEN" : ": E+W GREEN");
    }

public:
    TrafficController()
        : currentPhase(0), ticksRemaining(0)
    {
        for (int i = 0; i < 4; i++) lights[i].state = RED;
    }

    void addCar(int laneIndex, Node* car) {
        if (car->isEmergency) {
            // All lights → RED the moment an emergency vehicle is registered
            for (int i = 0; i < 4; i++) lights[i].state = RED;
            ticksRemaining = 0; // pause normal rotation
            emergencyQueue.enqueue(car);
            qDebug() << "🚨 Emergency queued:" << car->vehicleID
                     << "— all lights RED. Queue size:"
                     << emergencyQueue.peek()->vehicleID;
        } else {
            lanes[laneIndex].enqueue(car);
        }
    }

    LightState getLightState(int laneIndex) const {
        return lights[laneIndex].state;
    }

    bool hasEmergency() const { return !emergencyQueue.isEmpty(); }

    // Peek at the front emergency car's Node* so the window can find
    // which CarItem it belongs to (by pointer comparison).
    Node* peekEmergency() const {
        return emergencyQueue.peek();
    }

    // Call once per tick. If emergency queue is non-empty, keeps all
    // lights red and does nothing else. Normal rotation resumes only
    // when the queue is empty.
    void advanceLights() {
        // Emergency cars present → freeze all lights red, don't rotate
        if (!emergencyQueue.isEmpty()) {
            for (int i = 0; i < 4; i++) lights[i].state = RED;
            return;
        }

        if (!anyLaneHasCars()) return;

        if (ticksRemaining <= 0) {
            int next = 1 - currentPhase;
            if (!phaseHasCars(next)) next = currentPhase;
            activatePhase(next);
        }
        ticksRemaining--;
    }

    // Release the front emergency car. The caller passes in the laneIndex
    // (direction) of the matching CarItem so we can briefly green that lane.
    // Returns the Node* that was released.
    Node* releaseEmergency(int laneIndex) {
        if (emergencyQueue.isEmpty()) return nullptr;

        Node* car = emergencyQueue.dequeue();

        // Give that lane a momentary green so the visual light matches
        for (int i = 0; i < 4; i++) lights[i].state = RED;
        lights[laneIndex].state = GREEN;

        qDebug() << "🚑 Emergency THROUGH:" << car->vehicleID
                 << "— lane" << laneIndex << "briefly GREEN";

        // If more emergencies are still queued, re-freeze immediately
        if (!emergencyQueue.isEmpty()) {
            for (int i = 0; i < 4; i++) lights[i].state = RED;
        } else {
            // Resume normal rotation next tick
            ticksRemaining = 0;
        }

        return car;
    }

    // Release one normal car from a lane if its light is green
    Node* tryRelease(int laneIndex) {
        if (lights[laneIndex].state != GREEN) return nullptr;
        if (lanes[laneIndex].isEmpty())       return nullptr;
        Node* car = lanes[laneIndex].dequeue();
        qDebug() << "✅ Released:" << car->vehicleID << "from lane" << laneIndex;
        return car;
    }
};

#endif
