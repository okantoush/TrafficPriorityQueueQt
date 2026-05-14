#ifndef TRAFFICCONTROLLER_H
#define TRAFFICCONTROLLER_H

#include "Lane.h"
#include "PriorityQueue.h"
#include "Trafficlight.h"
#include "HashMap.h"

// Per-intersection traffic brain: owns four lane queues, two sets of lights
// (straight + protected left), an emergency-vehicle priority queue, and a
// 4-phase cycle that rotates green between N+S straight, N+S left, E+W
// straight, E+W left. Skips empty phases and scales green duration by queue
// size.
class TrafficController {
private:
    Lane          lanes[4];           // straight-through queues, one per direction
    TrafficLight  lights[4];          // straight-through lights
    TrafficLight  turnLights[4];      // protected left-turn lights
    PriorityQueue emergencyQueue;     // emergency vehicles (FIFO by arrival)

    HashMap carsCleared;              // total cars cleared, per direction
    HashMap historicalCongestion;     // peak queue size seen, per direction

    // Externally-updated queue sizes per direction. The window writes these
    // each tick from the actual scene state so the controller can size each
    // phase's green to demand. Under Dijkstra path-following the lane[]
    // queues stay empty, so straightQueueSize is what drives green
    // extension for normal cars.
    int turnQueueSize[4];
    int straightQueueSize[4];

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

    // Emergency-vehicle override: while active, advanceLights() does
    // nothing — the specific (dir, useTurn) light forced GREEN by
    // setEmergencyOverride stays lit and every other light stays RED.
    // The window calls setEmergencyOverride each tick while at least one
    // emergency is approaching or crossing this intersection, and
    // clearEmergencyOverride otherwise.
    bool m_emergencyOverrideActive;
    int  m_emergencyOverrideDir;     // 0=N..3=W
    bool m_emergencyOverrideTurn;    // true → use turnLights, false → lights
    // Phase durations in 50 ms ticks. Green is the longest by design so the
    // cycle reads as G → Y → R rather than feeling like a yellow flash.
    //   GREEN base (empty queue) =  80 ticks ≈ 4.0 s
    //   YELLOW                    =  25 ticks ≈ 1.25 s
    //   ALL-RED clearance         =  15 ticks ≈ 0.75 s
    static const int YELLOW_DURATION  = 25;
    static const int ALL_RED_DURATION = 15;

    bool phaseHasCars(int phase) const;
    bool anyLaneHasCars() const;
    int  nextPhase(int from) const;
    int  calculateGreenTicks();
    void activatePhase(int phase);

public:
    TrafficController();

    // IntersectionWindow updates these each tick based on actual CarItem counts.
    void setTurnQueueSize(int dir, int size);
    // Number of path-driven cars currently waiting at this intersection's
    // stop line in `dir`. Used to extend green when traffic is heavy.
    void setStraightQueueSize(int dir, int size);

    // Enqueue a car. Emergencies go into the priority queue and force all
    // lights red immediately; everything else goes to its direction's lane.
    void addCar(int laneIndex, Node* car);

    void updateCongestionStats();
    void recordCarCleared(int direction);

    LightState getLightState(int laneIndex) const;
    LightState getTurnLightState(int laneIndex) const;

    bool  hasEmergency() const;
    Node* peekEmergency() const;

    // Call once per tick. Handles the GREEN → YELLOW → ALL-RED → next GREEN
    // cycle through the 4 phases. If the emergency queue is non-empty, keeps
    // every light red until released.
    void advanceLights();

    // Pop the front emergency, force everything red except the emergency's
    // approach direction (briefly green).
    Node* releaseEmergency(int laneIndex);

    bool isYellow() const;

    // If `laneIndex` is currently green and has a queued car, dequeue it.
    Node* tryRelease(int laneIndex);

    // Emergency-vehicle preemption — bypass the normal phase cycle for as
    // long as an emergency is in or approaching the intersection. Call
    // setEmergencyOverride() each tick while the emergency is present
    // (the latest call wins), and clearEmergencyOverride() the first tick
    // none are present. When the override clears, the previous phase is
    // re-activated with a fresh green window so the cycle restarts cleanly.
    //
    //   dir          : 0=N, 1=E, 2=S, 3=W — the emergency's approach direction
    //   useTurnLight : true if the emergency is making a protected left
    //                  (light the turn signal); false for straight/right
    //                  (light the straight signal).
    void setEmergencyOverride(int dir, bool useTurnLight);
    void clearEmergencyOverride();
    bool isEmergencyOverrideActive() const;
};

#endif
