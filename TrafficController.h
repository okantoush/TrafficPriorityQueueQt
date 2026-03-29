#ifndef TRAFFICCONTROLLER_H
#define TRAFFICCONTROLLER_H

#include "Lane.h"
#include "PriorityQueue.h"
#include "TrafficLight.h"
#include "HashMap.h"
#include <QDebug>

class TrafficController {
private:
    Lane         lanes[4];
    TrafficLight lights[4];
    PriorityQueue emergencyQueue; // FIFO priority queue — first-in first-out for equal priority

    HashMap carsCleared;
    HashMap historicalCongestion;

    int  currentPhase;
    int  ticksRemaining;
    bool inYellow;
    int  yellowTicks;
    bool inAllRed;         // all-red clearance gap between phases
    int  allRedTicks;
    int  pendingPhase;     // which phase to activate after all-red
    static const int YELLOW_DURATION  = 40; // ~2 seconds at 50ms/tick
    static const int ALL_RED_DURATION = 30; // ~1.5 seconds clearance

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
        int baseTicks = 20;

        // Current immediate need
        int currentQueue = (currentPhase == 0)
                               ? qMax(lanes[0].getSize(), lanes[2].getSize())
                               : qMax(lanes[1].getSize(), lanes[3].getSize());

        // Historical need from Hash Map
        int historicalMax = (currentPhase == 0)
                                ? qMax(historicalCongestion.get(0), historicalCongestion.get(2))
                                : qMax(historicalCongestion.get(1), historicalCongestion.get(3));

        if (currentQueue == 0) return baseTicks;

        // Dynamic formula: Base time + (current queue * 10) + (historical backup * 2)
        int calculatedTicks = baseTicks + (currentQueue * 10) + (historicalMax * 2);

        // Cap at 200 so cross-traffic isn't permanently blocked
        return qMin(calculatedTicks, 200);
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
        : currentPhase(0), ticksRemaining(0),
          inYellow(false), yellowTicks(0),
          inAllRed(false), allRedTicks(0), pendingPhase(0)
    {
        for (int i = 0; i < 4; i++) lights[i].state = RED;
    }

    void addCar(int laneIndex, Node* car) {
        if (car->isEmergency) {
            // All lights → RED the moment an emergency vehicle is registered
            for (int i = 0; i < 4; i++) lights[i].state = RED;
            ticksRemaining = 0;
            inYellow = false;
            inAllRed = false;
            emergencyQueue.enqueue(car);
            qDebug() << "🚨 Emergency queued:" << car->vehicleID
                     << "— all lights RED. Queue size:"
                     << emergencyQueue.peek()->vehicleID;
        } else {
            lanes[laneIndex].enqueue(car);
        }
    }

    void updateCongestionStats() {
        for (int i = 0; i < 4; i++) {
            int currentSize = lanes[i].getSize();
            if (currentSize > historicalCongestion.get(i)) {
                historicalCongestion.put(i, currentSize);
            }
        }
    }

    void recordCarCleared(int direction) {
        carsCleared.increment(direction);
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

    // Call once per tick. Handles green → yellow → ALL RED → next green cycle.
    // If emergency queue is non-empty, keeps all lights red.
    void advanceLights() {
        // Emergency cars present → freeze all lights red, don't rotate
        if (!emergencyQueue.isEmpty()) {
            for (int i = 0; i < 4; i++) lights[i].state = RED;
            inYellow = false;
            inAllRed = false;
            return;
        }

        if (!anyLaneHasCars()) return;

        // All-red clearance phase — let intersection clear before next green
        if (inAllRed) {
            allRedTicks--;
            if (allRedTicks <= 0) {
                inAllRed = false;
                activatePhase(pendingPhase);
                qDebug() << "🔴 All-red clearance done → activating phase" << pendingPhase;
            }
            return;
        }

        // Yellow phase — count down, then enter all-red
        if (inYellow) {
            yellowTicks--;
            if (yellowTicks <= 0) {
                inYellow = false;
                for (int i = 0; i < 4; i++) lights[i].state = RED;

                // Enter all-red clearance before next green
                int next = 1 - currentPhase;
                if (!phaseHasCars(next)) next = currentPhase;
                pendingPhase = next;
                inAllRed = true;
                allRedTicks = ALL_RED_DURATION;
                qDebug() << "🔴 All-red clearance started";
            }
            return;
        }

        // Normal green countdown
        if (ticksRemaining <= 0) {
            int next = 1 - currentPhase;
            bool needSwitch = phaseHasCars(next) && (next != currentPhase);
            if (needSwitch) {
                // Go yellow on current green lanes
                if (currentPhase == 0) {
                    lights[0].state = YELLOW; lights[2].state = YELLOW;
                } else {
                    lights[1].state = YELLOW; lights[3].state = YELLOW;
                }
                inYellow = true;
                yellowTicks = YELLOW_DURATION;
                qDebug() << "🟡 Phase" << currentPhase << "→ YELLOW";
                return;
            }
            // No cars in the other phase — just re-activate current
            activatePhase(currentPhase);
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

    bool isYellow() const { return inYellow; }

    // Release one normal car from a lane if its light is green (not yellow/red)
    Node* tryRelease(int laneIndex) {
        if (lights[laneIndex].state != GREEN) return nullptr;
        if (lanes[laneIndex].isEmpty())       return nullptr;
        Node* car = lanes[laneIndex].dequeue();
        qDebug() << "✅ Released:" << car->vehicleID << "from lane" << laneIndex;
        return car;
    }
};

#endif
