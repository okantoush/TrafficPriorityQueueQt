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

    // Create movement path depedning on road type (if turn or straight line)
//    m_path.moveTo(start->getPosition());

//    m_path.lineTo(end->getPosition());

    QPointF p1 = start->getPosition();
    QPointF p2 = end->getPosition();

    m_path.moveTo(p1);

    // Check if turn edge
    bool isTurn = (p1.x() != p2.x()) && (p1.y() != p2.y());

    if (isTurn)
    {
        // Make curved turn
        QPointF controlPoint(
            (p1.x() + p2.x()) / 2,
            (p1.y() + p2.y()) / 2); //finds middle point between p1 & p2 to make vehicle path look like a curve

        m_path.quadTo(controlPoint, p2);
    }
    else
    {
        // Is straight road
        m_path.lineTo(p2);
    }
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
