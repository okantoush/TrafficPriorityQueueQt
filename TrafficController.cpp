#include "TrafficController.h"

#include <QDebug>
#include <QtGlobal>

// ── Private helpers ──────────────────────────────────────────────────────

bool TrafficController::phaseHasCars(int phase) const {
    // Straight phases also see externally-pushed path-car queues; turn
    // phases see only the turn count IntersectionWindow pushes for cars
    // intending to make a left.
    if (phase == 0) return !lanes[0].isEmpty() || !lanes[2].isEmpty()
                        || straightQueueSize[0] > 0 || straightQueueSize[2] > 0;
    if (phase == 1) return turnQueueSize[0] > 0 || turnQueueSize[2] > 0;
    if (phase == 2) return !lanes[1].isEmpty() || !lanes[3].isEmpty()
                        || straightQueueSize[1] > 0 || straightQueueSize[3] > 0;
    /* phase == 3 */ return turnQueueSize[1] > 0 || turnQueueSize[3] > 0;
}

bool TrafficController::anyLaneHasCars() const {
    for (int i = 0; i < 4; i++)
        if (!lanes[i].isEmpty()) return true;
    for (int i = 0; i < 4; i++)
        if (turnQueueSize[i] > 0 || straightQueueSize[i] > 0) return true;
    return false;
}

// Pick the next phase to activate. Prefer the next phase that actually has
// cars (so the light doesn't sit on an empty turn-only phase while traffic
// piles up elsewhere). When nothing is queued anywhere — which is the steady
// state under Dijkstra path-following, since path cars don't enter the lane
// queues — fall through to the next phase anyway so the cycle keeps rotating
// visibly instead of freezing.
int TrafficController::nextPhase(int from) const {
    for (int step = 1; step <= 4; step++) {
        int candidate = (from + step) % 4;
        if (phaseHasCars(candidate)) return candidate;
    }
    return (from + 1) % 4;
}

int TrafficController::calculateGreenTicks() {
    // Empty-queue minimum green. Stays longer than the yellow + all-red
    // transition so the light actually feels green when no traffic is around.
    int baseTicks = 80;

    // Straight-through phases: queue size = max(lane queue, externally-
    // pushed path-car waiting count). Path cars don't enter lanes[] so the
    // straight queue is what makes green respond to traffic under Dijkstra.
    if (currentPhase == 0 || currentPhase == 2) {
        int currentQueue = (currentPhase == 0)
                               ? qMax(qMax(lanes[0].getSize(), lanes[2].getSize()),
                                      qMax(straightQueueSize[0], straightQueueSize[2]))
                               : qMax(qMax(lanes[1].getSize(), lanes[3].getSize()),
                                      qMax(straightQueueSize[1], straightQueueSize[3]));
        int historicalMax = (currentPhase == 0)
                                ? qMax(historicalCongestion.get(0), historicalCongestion.get(2))
                                : qMax(historicalCongestion.get(1), historicalCongestion.get(3));
        if (currentQueue == 0) return baseTicks;
        int calc = baseTicks + (currentQueue * 10) + (historicalMax * 2);
        return qMin(calc, 200);
    }

    // Turn phases: scale duration with the number of turners waiting.
    // Each Bezier turn takes ~40 ticks, so give ~40 per car plus overhead.
    int turnCount = (currentPhase == 1)
                        ? qMax(turnQueueSize[0], turnQueueSize[2])
                        : qMax(turnQueueSize[1], turnQueueSize[3]);
    if (turnCount == 0) return 20;
    int calc = 30 + turnCount * 45;
    return qMin(calc, 180);
}

void TrafficController::activatePhase(int phase) {
    currentPhase = phase;
    for (int i = 0; i < 4; i++) { lights[i].state = RED; turnLights[i].state = RED; }

    switch (phase) {
    case 0: lights[0].state     = GREEN; lights[2].state     = GREEN; break; // N+S straight
    case 1: turnLights[0].state = GREEN; turnLights[2].state = GREEN; break; // N+S turn
    case 2: lights[1].state     = GREEN; lights[3].state     = GREEN; break; // E+W straight
    case 3: turnLights[1].state = GREEN; turnLights[3].state = GREEN; break; // E+W turn
    }
    ticksRemaining = calculateGreenTicks();

    const char* names[4] = { "N+S straight", "N+S left-turn",
                             "E+W straight", "E+W left-turn" };
    qDebug() << "🟢 Phase" << phase << ":" << names[phase]
             << "for" << ticksRemaining << "ticks";
}

// ── Public API ───────────────────────────────────────────────────────────

TrafficController::TrafficController()
    : currentPhase(0), ticksRemaining(0),
      inYellow(false), yellowTicks(0),
      inAllRed(false), allRedTicks(0), pendingPhase(0),
      m_emergencyOverrideActive(false),
      m_emergencyOverrideDir(-1),
      m_emergencyOverrideTurn(false)
{
    for (int i = 0; i < 4; i++) { lights[i].state = RED; turnLights[i].state = RED; }
    for (int i = 0; i < 4; i++) { turnQueueSize[i] = 0; straightQueueSize[i] = 0; }
    // Start in N+S-straight green so the visible state matches currentPhase.
    activatePhase(0);
}

void TrafficController::setTurnQueueSize(int dir, int size) {
    if (dir >= 0 && dir < 4) turnQueueSize[dir] = size;
}

void TrafficController::setStraightQueueSize(int dir, int size) {
    if (dir >= 0 && dir < 4) straightQueueSize[dir] = size;
}

void TrafficController::addCar(int laneIndex, Node* car) {
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

void TrafficController::updateCongestionStats() {
    for (int i = 0; i < 4; i++) {
        int currentSize = lanes[i].getSize();
        if (currentSize > historicalCongestion.get(i)) {
            historicalCongestion.put(i, currentSize);
        }
    }
}

void TrafficController::recordCarCleared(int direction) {
    carsCleared.increment(direction);
}

LightState TrafficController::getLightState(int laneIndex) const {
    return lights[laneIndex].state;
}

LightState TrafficController::getTurnLightState(int laneIndex) const {
    return turnLights[laneIndex].state;
}

bool TrafficController::hasEmergency() const {
    return !emergencyQueue.isEmpty();
}

Node* TrafficController::peekEmergency() const {
    return emergencyQueue.peek();
}

void TrafficController::advanceLights() {
    // Emergency preemption — while the window is signalling that an
    // emergency is approaching/crossing, the override has already pinned
    // the right light green and every other light red. Nothing to do
    // until the window clears it.
    if (m_emergencyOverrideActive) {
        return;
    }

    if (!emergencyQueue.isEmpty()) {
        for (int i = 0; i < 4; i++) { lights[i].state = RED; turnLights[i].state = RED; }
        inYellow = false;
        inAllRed = false;
        return;
    }

    // (The historical "if (!anyLaneHasCars()) return;" guard was removed so
    //  the 4-phase cycle keeps rotating visibly under Dijkstra path-following,
    //  where the per-direction lane queues stay permanently empty.)

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
            // Turn whichever lights are currently GREEN to YELLOW.
            for (int i = 0; i < 4; i++) {
                if (lights[i].state     == GREEN) lights[i].state     = YELLOW;
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

Node* TrafficController::releaseEmergency(int laneIndex) {
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

bool TrafficController::isYellow() const {
    return inYellow;
}

Node* TrafficController::tryRelease(int laneIndex) {
    if (lights[laneIndex].state != GREEN) return nullptr;
    if (lanes[laneIndex].isEmpty())       return nullptr;
    Node* car = lanes[laneIndex].dequeue();
    qDebug() << "✅ Released:" << car->vehicleID << "from lane" << laneIndex;
    return car;
}

// ── Emergency preemption ────────────────────────────────────────────────

void TrafficController::setEmergencyOverride(int dir, bool useTurnLight) {
    if (dir < 0 || dir > 3) return;
    m_emergencyOverrideActive = true;
    m_emergencyOverrideDir    = dir;
    m_emergencyOverrideTurn   = useTurnLight;

    // Force every light red, then flip ONLY the emergency's light green.
    // The normal phase cycle is frozen until clearEmergencyOverride().
    for (int i = 0; i < 4; i++) {
        lights[i].state     = RED;
        turnLights[i].state = RED;
    }
    if (useTurnLight) turnLights[dir].state = GREEN;
    else              lights[dir].state     = GREEN;
}

void TrafficController::clearEmergencyOverride() {
    if (!m_emergencyOverrideActive) return;
    m_emergencyOverrideActive = false;
    m_emergencyOverrideDir    = -1;
    m_emergencyOverrideTurn   = false;
    // Re-activate the current phase so its full green window restarts
    // cleanly — without this the lights would still show whatever the
    // override left them as until the next phase transition.
    activatePhase(currentPhase);
}

bool TrafficController::isEmergencyOverrideActive() const {
    return m_emergencyOverrideActive;
}
