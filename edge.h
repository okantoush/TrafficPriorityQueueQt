#ifndef EDGE_H
#define EDGE_H

#include <QPainterPath>

class GraphNode;

class Edge
{
public:
    Edge(GraphNode* start, GraphNode* end);

    GraphNode* getStartNode() const;

    GraphNode* getEndNode() const;

    float getWeight() const;

    QPainterPath getPath() const;

private:
    GraphNode* m_start;

    GraphNode* m_end;

    float m_weight;

    QPainterPath m_path;
};

#endif // EDGE_H
