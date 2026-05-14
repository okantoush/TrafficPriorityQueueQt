#include "randomroutegenerator.h"
#include "graphmanager.h"
#include "graphnode.h"
#include <QRandomGenerator>

static const int kEntryNodeIds[8] = { 101, 107, 113, 119, 125, 131, 137, 143 };
static const int kExitNodeIds[8]  = { 106, 112, 118, 124, 130, 136, 142, 148 };

RandomRouteGenerator::StartChoice RandomRouteGenerator::generateRandomRoad(GraphManager* gm)
{
    if (!gm) return { nullptr, -1 };
    const int ri = QRandomGenerator::global()->bounded(8);
    return { gm->getNodeByID(kEntryNodeIds[ri]), ri };
}

RandomRouteGenerator::DestChoice RandomRouteGenerator::generateDestination(GraphManager* gm,
                                                                           int startRoadIndex)
{
    if (!gm || startRoadIndex < 0 || startRoadIndex >= 8)
        return { nullptr, -1 };
    // Exit is the far end of the *same* directed lane as the start (straight drive).
    // A random exit on another road would never be reached and broke removal logic.
    return { gm->getNodeByID(kExitNodeIds[startRoadIndex]), startRoadIndex };
}
