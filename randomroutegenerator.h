#ifndef RANDOMROUTEGENERATOR_H
#define RANDOMROUTEGENERATOR_H

class GraphManager;
class GraphNode;

// Matches the eight directed lane chains in buildInitialGraph().
class RandomRouteGenerator {
public:
    struct StartChoice {
        GraphNode* node;
        int        roadIndex; // 0..7
    };
    struct DestChoice {
        GraphNode* node;
        int        roadIndex;
    };

    static StartChoice generateRandomRoad(GraphManager* gm);
    // Opposite end of the same directed lane as startRoadIndex (reachable by straight driving).
    static DestChoice generateDestination(GraphManager* gm, int startRoadIndex);
};

#endif
