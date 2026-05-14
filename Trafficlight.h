#ifndef TRAFFICLIGHT_H
#define TRAFFICLIGHT_H

enum LightState {
    RED,
    GREEN,
    YELLOW
};

class TrafficLight {
public:
    LightState state;
    int greenTime;

    TrafficLight() {
        state = RED;
        greenTime = 5;
    }
};

#endif
