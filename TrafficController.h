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
    TrafficLight lights[4];        // straight-through lights
    TrafficLight turnLights[4];    // protected left-turn lights
    PriorityQueue emergencyQueue;

    HashMap carsCleared;
    HashMap historicalCongestion;

    // Externally-updated queue size of left-turn cars per direction.
    // Used to decide turn-phase duration (longer when more cars waiting).
    int turnQueueSize[4];

    // 4-phase cycle:
    //   0 = N+S straight green
    //   1 = N+S turn green
    //   2 = E+W straight green
    //   3 = E+W turn green
    int  currentPhase;
    int  ticksRemaining;
    bool inYellow;
    int  yellowTicks;
    bool inAllRed;
    int  allRedTicks;
    int  pendingPhase;
    static const int YELLOW_DURATION  = 40;
    static const int ALL_RED_DURATION = 30;

    bool phaseHasCars(int phase) const {
        if (phase == 0) return !lanes[0].isEmpty() || !lanes[2].isEmpty();  // N+S straight
        if (phase == 1) return turnQueueSize[0] > 0 || turnQueueSize[2] > 0; // N+S turn
        if (phase == 2) return !lanes[1].isEmpty() || !lanes[3].isEmpty();  // E+W straight
        /* phase == 3 */ return turnQueueSize[1] > 0 || turnQueueSize[3] > 0;// E+W turn
    }

    bool anyLaneHasCars() const {
        for (int i = 0; i < 4; i++)
            if (!lanes[i].isEmpty()) return true;
        for (int i = 0; i < 4; i++)
            if (turnQueueSize[i] > 0) return true;
        return false;
    }

    // Pick the next phase to activate. Skip empty phases so light doesn't
    // sit on an empty turn-only phase if nobody's waiting to turn.
    int nextPhase(int from) const {
        for (int step = 1; step <= 4; step++) {
            int candidate = (from + step) % 4;
            if (phaseHasCars(candidate)) return candidate;
        }
        return from;   // nothing anywhere — stay put
    }

    int calculateGreenTicks() {
        int baseTicks = 20;

        // Straight-through phases: use lane queue sizes + historical peak
        if (currentPhase == 0 || currentPhase == 2) {
            int currentQueue = (currentPhase == 0)
                                   ? qMax(lanes[0].getSize(), lanes[2].getSize())
                                   : qMax(lanes[1].getSize(), lanes[3].getSize());
            int historicalMax = (currentPhase == 0)
                                    ? qMax(historicalCongestion.get(0), historicalCongestion.get(2))
                                    : qMax(historicalCongestion.get(1), historicalCongestion.get(3));
            if (currentQueue == 0) return baseTicks;
            int calc = baseTicks + (currentQueue * 10) + (historicalMax * 2);
            return qMin(calc, 200);
        }

        // Turn phases: scale duration with number of turn-cars waiting.
        // Each turn takes ~40 ticks (Bezier), so give ~40 per car plus overhead.
        int turnCount = (currentPhase == 1)
                            ? qMax(turnQueueSize[0], turnQueueSize[2])
                            : qMax(turnQueueSize[1], turnQueueSize[3]);
        if (turnCount == 0) return 20;     // minimum window
        int calc = 30 + turnCount * 45;     // enough time for each turner to clear
        return qMin(calc, 180);
    }

    void activatePhase(int phase) {
        currentPhase = phase;
        for (int i = 0; i < 4; i++) { lights[i].state = RED; turnLights[i].state = RED; }

        switch (phase) {
        case 0: lights[0].state = GREEN; lights[2].state = GREEN; break;   // N+S straight
        case 1: turnLights[0].state = GREEN; turnLights[2].state = GREEN; break; // N+S turn
        case 2: lights[1].state = GREEN; lights[3].state = GREEN; break;   // E+W straight
        case 3: turnLights[1].state = GREEN; turnLights[3].state = GREEN; break; // E+W turn
        }
        ticksRemaining = calculateGreenTicks();

        const char* names[4] = { "N+S straight", "N+S left-turn",
                                 "E+W straight", "E+W left-turn" };
        qDebug() << "🟢 Phase" << phase << ":" << names[phase]
                 << "for" << ticksRemaining << "ticks";
    }

public:
    TrafficController()
        : currentPhase(0), ticksRemaining(0),
          inYellow(false), yellowTicks(0),
          inAllRed(false), allRedTicks(0), pendingPhase(0)
    {
        for (int i = 0; i < 4; i++) { lights[i].state = RED; turnLights[i].state = RED; }
        for (int i = 0; i < 4; i++) turnQueueSize[i] = 0;
    }

    // IntersectionWindow updates these each tick based on actual CarItem counts.
    void setTurnQueueSize(int dir, int size) {
        if (dir >= 0 && dir < 4) turnQueueSize[dir] = size;
    }

    void addCar(int laneIndex, Node* car) {
        if (car->isEmergency) {
            for (int i = 0; i < 4; i++) { lights[i].state = RED; turnLights[i].state = RED; }
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

    LightState getTurnLightState(int laneIndex) const {
        return turnLights[laneIndex].state;
    }

    bool hasEmergency() const { return !emergencyQueue.isEmpty(); }
    Node* peekEmergency() const { return emergencyQueue.peek(); }

    // Call once per tick. Handles green → yellow → all-red → next green cycle
    // through the 4-phase rotation. If emergency queue is non-empty, keeps all
    // lights red.
    void advanceLights() {
        if (!emergencyQueue.isEmpty()) {
            for (int i = 0; i < 4; i++) { lights[i].state = RED; turnLights[i].state = RED; }
            inYellow = false;
            inAllRed = false;
            return;
        }

        if (!anyLaneHasCars()) return;

        if (inAllRed) {
            allRedTicks--;
            if (allRedTicks <= 0) {
                inAllRed = false;
                activatePhase(pendingPhase);
                qDebug() << "🔴 All-red done → phase" << pendingPhase;
            }
            return;
        }

        if (inYellow) {
            yellowTicks--;
            if (yellowTicks <= 0) {
                inYellow = false;
                for (int i = 0; i < 4; i++) { lights[i].state = RED; turnLights[i].state = RED; }
                pendingPhase = nextPhase(currentPhase);
                inAllRed = true;
                allRedTicks = ALL_RED_DURATION;
                qDebug() << "🔴 All-red clearance started";
            }
            return;
        }

        if (ticksRemaining <= 0) {
            int next = nextPhase(currentPhase);
            if (next != currentPhase) {
                // Turn whichever lights are currently GREEN to YELLOW
                for (int i = 0; i < 4; i++) {
                    if (lights[i].state == GREEN)     lights[i].state = YELLOW;
                    if (turnLights[i].state == GREEN) turnLights[i].state = YELLOW;
                }
                inYellow = true;
                yellowTicks = YELLOW_DURATION;
                qDebug() << "🟡 Phase" << currentPhase << "→ YELLOW";
                return;
            }
            activatePhase(currentPhase);
        }
        ticksRemaining--;
    }

    Node* releaseEmergency(int laneIndex) {
        if (emergencyQueue.isEmpty()) return nullptr;
        Node* car = emergencyQueue.dequeue();
        for (int i = 0; i < 4; i++) { lights[i].state = RED; turnLights[i].state = RED; }
        lights[laneIndex].state = GREEN;
        qDebug() << "🚑 Emergency THROUGH:" << car->vehicleID
                 << "— lane" << laneIndex << "briefly GREEN";
        if (!emergencyQueue.isEmpty()) {
            for (int i = 0; i < 4; i++) { lights[i].state = RED; turnLights[i].state = RED; }
        } else {
            ticksRemaining = 0;
        }
        return car;
    }

    bool isYellow() const { return inYellow; }

    Node* tryRelease(int laneIndex) {
        if (lights[laneIndex].state != GREEN) return nullptr;
        if (lanes[laneIndex].isEmpty())       return nullptr;
        Node* car = lanes[laneIndex].dequeue();
        qDebug() << "✅ Released:" << car->vehicleID << "from lane" << laneIndex;
        return car;
    }
};

#endif
