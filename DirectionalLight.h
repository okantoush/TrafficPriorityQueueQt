#ifndef DIRECTIONALLIGHT_H
#define DIRECTIONALLIGHT_H

#include <QGraphicsItem>
#include <QPainter>
#include <QRectF>
#include <QColor>
#include <QPointF>

// A single traffic-light housing rendered with a colored lens facing a
// specific direction (the side of the housing that drivers approaching
// from that direction can see):
//   dir=0  North → lens faces DOWN
//   dir=1  East  → lens faces LEFT
//   dir=2  South → lens faces UP
//   dir=3  West  → lens faces RIGHT

class DirectionalLight : public QGraphicsItem
{
public:
    DirectionalLight(int dir, QPointF topLeft);

    void setColor(QColor c);

    QRectF boundingRect() const override;
    void   paint(QPainter* p,
                 const QStyleOptionGraphicsItem* opt,
                 QWidget* widget) override;

private:
    int    m_dir;
    QColor m_color;

    // Housing dimensions — square so it looks the same regardless of rotation.
    static constexpr qreal W   = 22;
    static constexpr qreal H   = 22;
    static constexpr qreal PAD = 3;
};

#endif
