#ifndef GRAPHNODE_H
#define GRAPHNODE_H

#include <QGraphicsEllipseItem>
#include <QPainter>
#include <QPixmap>
#include <QPointF>
#include <QString>
#include <vector>

class Edge;

class GraphNode : public QGraphicsEllipseItem
{
public:
    // Visual state, used by the interactive-Dijkstra animation. Default
    // = the faint yellow appearance; the other values temporarily paint
    // the node a distinct color and bring it forward in z-order.
    enum VisualState {
        VS_DEFAULT  = 0,
        VS_START    = 1,
        VS_END      = 2,
        VS_SETTLED  = 3,
        VS_FRONTIER = 4,
        VS_CURRENT  = 5,
        VS_PATH     = 6
    };

    GraphNode(int id, QPointF position);
    // Roadside hospital: sits in the central block of a tile, connected to the graph.
    GraphNode(int id, QPointF position, const QString& hospitalName);

    int getID() const;
    bool isHospital() const { return m_isHospital; }
    QPointF getPosition() const;
    void addEdge(Edge* edge);
    std::vector<Edge*> getOutgoingEdges() const;

    void setVisualState(int state);
    int  visualState() const { return m_visualState; }

protected:
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;

private:
    int m_id;
    QPointF m_position;
    std::vector<Edge*> m_outgoingEdges;
    int m_visualState;
    bool m_isHospital = false;
    QString m_hospitalName;
    QPixmap m_hospitalIcon;
};

#endif // GRAPHNODE_H
