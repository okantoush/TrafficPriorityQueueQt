#include "edge.h"
#include "graphnode.h"

#include <QLineF>

Edge::Edge(GraphNode* start, GraphNode* end)
    : m_start(start),
    m_end(end)
{
    // Automatic edge weight
    m_weight = QLineF(
                   start->getPosition(),
                   end->getPosition()
                   ).length();

    // Create straight movement path
    m_path.moveTo(start->getPosition());

    m_path.lineTo(end->getPosition());
}

GraphNode* Edge::getStartNode() const
{
    return m_start;
}

GraphNode* Edge::getEndNode() const
{
    return m_end;
}

float Edge::getWeight() const
{
    return m_weight;
}

QPainterPath Edge::getPath() const
{
    return m_path;
}
