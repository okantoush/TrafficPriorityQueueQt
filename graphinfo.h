#ifndef GRAPHINFO_H
#define GRAPHINFO_H

#include <QHash>
#include <QVector>

// Runtime lookup tables populated by IntersectionWindow::buildInitialGraph
// when the road graph is generated for an arbitrary rows × cols grid.
//
// CarItem's static helpers (approachNodeInfo, nodeChain, chainDirection,
// turnIntentForApproach) read from a single shared GraphInfo* instead of
// hardcoded switch statements, so the same path-following / Bezier /
// blinker / red-light logic in CarItem works unchanged for any grid size.
struct GraphInfo {
    struct Approach {
        int intId;   // intersection index = row * cols + col
        int dir;     // approach direction: 0=N, 1=E, 2=S, 3=W
    };

    // Approach nodes: nodeId → (intersection index, direction the car
    // is HEADING when it reaches this node — i.e. the approach side).
    QHash<int, Approach> approachNodes;

    // Each graph node belongs to exactly one directed-lane chain.
    QHash<int, int> nodeChain;

    // Each chain heads in one cardinal direction (0=N..3=W).
    QHash<int, int> chainDirection;

    // Perimeter entry/exit IDs — used by the random-route spawn picker.
    QVector<int> entryNodes;
    QVector<int> exitNodes;

    // Maps each entry node to the tile that "owns" it. Used only for the
    // per-intersection car-count HUD; the simulation doesn't care which
    // tile owns a path car beyond bookkeeping.
    QHash<int, int> entryToTile;

    void clear() {
        approachNodes.clear();
        nodeChain.clear();
        chainDirection.clear();
        entryNodes.clear();
        exitNodes.clear();
        entryToTile.clear();
    }
};

#endif
