#include "graphnode.h"
#include "edge.h"

#include <QBrush>
#include <QColor>
#include <QFont>
#include <QFontMetricsF>
#include <QGraphicsTextItem>
#include <QPainter>
#include <QPen>
#include <QString>
#include <QRectF>
#include <QSizeF>

static QPixmap makeHospitalIconFallbackPixmap()
{
    const int s = 40;
    QPixmap pm(s, s);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(235, 245, 255));
    p.drawRoundedRect(2, 2, s - 4, s - 4, 6, 6);
    p.setBrush(QColor(220, 60, 60));
    const qreal u = s / 16.0;
    p.drawRect(QRectF(7 * u, 4 * u, 2 * u, 8 * u));
    p.drawRect(QRectF(4 * u, 7 * u, 8 * u, 2 * u));
    p.setBrush(QColor(40, 120, 200));
    p.drawRect(QRectF(11 * u, 11 * u, 4 * u, 3 * u));
    return pm;
}

static QPixmap loadHospitalImagePixmap()
{
    const int target = 120;
    QPixmap pm(QStringLiteral(":/images/hospital.png"));
    if (pm.isNull()) {
        return makeHospitalIconFallbackPixmap().scaled(target, target, Qt::KeepAspectRatio,
                                                       Qt::SmoothTransformation);
    }
    return pm.scaled(target, target, Qt::KeepAspectRatio, Qt::SmoothTransformation);
}

GraphNode::GraphNode(int id, QPointF position)
    : m_id(id),
      m_position(position),
      m_visualState(VS_DEFAULT),
      m_isHospital(false)
{
    static const qreal NODE_RADIUS = 24.0;

    setRect(-NODE_RADIUS, -NODE_RADIUS, NODE_RADIUS * 2, NODE_RADIUS * 2);
    setPos(position);
    setBrush(QBrush(QColor(255, 245, 80, 110)));
    setPen(QPen(QColor(0, 0, 0, 180), 2));
    setZValue(5);
    setToolTip(QString("Graph node %1  (%2, %3)")
                   .arg(m_id)
                   .arg(position.x(), 0, 'f', 0)
                   .arg(position.y(), 0, 'f', 0));

    QGraphicsTextItem* label = new QGraphicsTextItem(QString::number(m_id), this);
    QFont labelFont("Helvetica", 12, QFont::Bold);
    label->setFont(labelFont);
    label->setDefaultTextColor(QColor(0, 0, 0, 220));
    QRectF labelBox = label->boundingRect();
    label->setPos(-labelBox.width() / 2.0, -labelBox.height() / 2.0);
    label->setZValue(6);
}

GraphNode::GraphNode(int id, QPointF position, const QString& hospitalName)
    : m_id(id),
      m_position(position),
      m_visualState(VS_DEFAULT),
      m_isHospital(true),
      m_hospitalName(hospitalName.trimmed().isEmpty() ? QStringLiteral("Hospital") : hospitalName.trimmed()),
      m_hospitalIcon(loadHospitalImagePixmap())
{
    const qreal w = 236.0;
    const qreal h = 178.0;
    setRect(-w / 2, -h / 2, w, h);
    setPos(position);
    setBrush(Qt::NoBrush);
    setPen(QPen(QColor(40, 90, 140), 2));
    setZValue(7);
    setToolTip(QStringLiteral("%1\n(Graph node %2)")
                   .arg(m_hospitalName)
                   .arg(m_id));
}

int GraphNode::getID() const
{
    return m_id;
}

QPointF GraphNode::getPosition() const
{
    return scenePos();
}

void GraphNode::addEdge(Edge* edge)
{
    m_outgoingEdges.push_back(edge);
}

std::vector<Edge*> GraphNode::getOutgoingEdges() const
{
    return m_outgoingEdges;
}

void GraphNode::setVisualState(int state)
{
    m_visualState = state;
    if (m_isHospital) {
        prepareGeometryChange();
        switch (state) {
        case VS_START:     setPen(QPen(QColor(20, 140, 40), 3));   setZValue(12); break;
        case VS_END:       setPen(QPen(QColor(180, 30, 30), 3));   setZValue(12); break;
        case VS_SETTLED:   setPen(QPen(QColor(50, 120, 180), 2));  setZValue(11); break;
        case VS_FRONTIER:  setPen(QPen(QColor(200, 110, 30), 2));  setZValue(11); break;
        case VS_CURRENT:   setPen(QPen(QColor(200, 160, 20), 3));   setZValue(12); break;
        case VS_PATH:      setPen(QPen(QColor(0, 160, 200), 3));    setZValue(12); break;
        case VS_DEFAULT:
        default:           setPen(QPen(QColor(40, 90, 140), 2));    setZValue(7);  break;
        }
        update();
        return;
    }

    QColor fill;
    QColor border = QColor(0, 0, 0, 220);
    qreal  borderW = 2;
    qreal  z       = 5;

    switch (state) {
    case VS_START:     fill = QColor( 76, 220,  90, 255); border = QColor(20, 70, 20, 255); borderW = 3; z = 12; break;
    case VS_END:       fill = QColor(240,  70,  70, 255); border = QColor(80, 20, 20, 255); borderW = 3; z = 12; break;
    case VS_SETTLED:   fill = QColor( 90, 180, 230, 220); border = QColor(20, 50, 80, 220); borderW = 2; z = 11; break;
    case VS_FRONTIER:  fill = QColor(255, 170,  60, 230); border = QColor(80, 50, 10, 220); borderW = 2; z = 11; break;
    case VS_CURRENT:   fill = QColor(255, 240,  60, 255); border = QColor(80, 60, 10, 255); borderW = 3; z = 12; break;
    case VS_PATH:      fill = QColor(  0, 200, 230, 255); border = QColor( 0, 50,  80, 255); borderW = 3; z = 12; break;
    case VS_DEFAULT:
    default:           fill = QColor(255, 245,  80, 110); border = QColor( 0,  0,  0, 180); borderW = 2; z = 5;  break;
    }
    setBrush(QBrush(fill));
    setPen(QPen(border, borderW));
    setZValue(z);
    update();
}

void GraphNode::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget)
{
    if (!m_isHospital) {
        QGraphicsEllipseItem::paint(painter, option, widget);
        return;
    }

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);

    QColor ring(40, 90, 140, 180);
    switch (m_visualState) {
    case VS_START:     ring = QColor(76, 220, 90, 240);   break;
    case VS_END:       ring = QColor(240, 70, 70, 240);   break;
    case VS_SETTLED:   ring = QColor(90, 180, 230, 220);   break;
    case VS_FRONTIER:  ring = QColor(255, 170, 60, 230);  break;
    case VS_CURRENT:   ring = QColor(255, 240, 60, 240);    break;
    case VS_PATH:      ring = QColor(0, 200, 230, 255);    break;
    default: break;
    }

    const QRectF r = rect();
    const qreal iconY = r.top() + 4;
    const QPointF iconTopLeft(r.center().x() - m_hospitalIcon.width() / 2.0, iconY);
    painter->setPen(QPen(ring, pen().widthF()));
    painter->setBrush(Qt::NoBrush);
    painter->drawRoundedRect(QRectF(iconTopLeft - QPointF(4, 4),
                                    QSizeF(m_hospitalIcon.width() + 8, m_hospitalIcon.height() + 8)), 8, 8);
    painter->drawPixmap(iconTopLeft.toPoint(), m_hospitalIcon);

    QFont f("Helvetica", 14, QFont::Bold);
    painter->setFont(f);
    painter->setPen(QPen(QColor(20, 40, 70), 1));
    const qreal maxW = r.width() - 6;
    QFontMetricsF fm(f);
    QString line = m_hospitalName;
    if (fm.horizontalAdvance(line) > maxW) {
        while (line.length() > 3 && fm.horizontalAdvance(line + QStringLiteral("…")) > maxW)
            line.chop(1);
        line += QStringLiteral("…");
    }
    const qreal textY = iconTopLeft.y() + m_hospitalIcon.height() + 6;
    painter->drawText(QRectF(r.left() + 3, textY, r.width() - 6, r.bottom() - textY - 2),
                      int(Qt::AlignHCenter | Qt::AlignTop), line);

    painter->restore();
}
