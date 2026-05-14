#ifndef GRAPHMANAGER_H
#define GRAPHMANAGER_H

#include <vector>

class GraphNode;
class Edge;

class GraphManager
{
public:
    GraphManager();
    ~GraphManager();

    // Add node to graph
    void addNode(GraphNode* node);

    // Connect two nodes with an edge
    void connectNodes(GraphNode* start, GraphNode* end);

    // Clear graph storage. The QGraphicsScene owns/deletes node items,
    // so this only deletes Edge objects and forgets node pointers.
    void clear();

    // Get all nodes
    std::vector<GraphNode*> getNodes() const;

    // Get all edges
    std::vector<Edge*> getEdges() const;

    // Find node by ID
    GraphNode* getNodeByID(int id) const;

    // Dijkstra shortest path from start → end using each edge's Euclidean
    // weight. Returns the full sequence of nodes (start first, end last),
    // or an empty vector if no path exists.
    std::vector<GraphNode*> shortestPath(GraphNode* start, GraphNode* end) const;

private:
    std::vector<GraphNode*> m_nodes;

    std::vector<Edge*> m_edges;
};

#endif // GRAPHMANAGER_H
