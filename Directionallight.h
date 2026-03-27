#ifndef DIRECTIONALLIGHT_H
#define DIRECTIONALLIGHT_H

#include <QGraphicsItem>
#include <QPainter>
#include <QRectF>

// Traffic light housing sitting at intersection corners (off the road).
// The lens is on the face pointing TOWARD the incoming cars.
//
//  dir=0  North lane (cars travel UP from bottom)
//         → light at top-right corner, lens on BOTTOM face (faces down toward cars)
//
//  dir=1  East lane (cars travel RIGHT from left)
//         → light at top-left corner, lens on RIGHT face (faces right toward cars)
//         Wait — East cars come FROM the left, so the light must face LEFT (toward x=0)
//         → lens on LEFT face
//
//  dir=2  South lane (cars travel DOWN from top)
//         → light at bottom-left corner, lens on TOP face (faces up toward cars)
//
//  dir=3  West lane (cars travel LEFT from right)
//         → light at bottom-right corner, lens on LEFT face ... no:
//         West cars come FROM the right, so the light faces RIGHT (toward x=600)
//         → lens on RIGHT face
//
// Summary of open/lens face per direction:
//   dir=0  North → lens faces DOWN   (south face of housing)
//   dir=1  East  → lens faces LEFT   (west  face of housing)
//   dir=2  South → lens faces UP     (north face of housing)
//   dir=3  West  → lens faces RIGHT  (east  face of housing)

class DirectionalLight : public QGraphicsItem
{
public:
    DirectionalLight(int dir, QPointF topLeft)
        : m_dir(dir), m_color(Qt::red)
    {
        setPos(topLeft);
        setZValue(25);
    }

    void setColor(QColor c) { m_color = c; update(); }

    QRectF boundingRect() const override {
        return QRectF(0, 0, W, H);
    }

    void paint(QPainter* p, const QStyleOptionGraphicsItem*, QWidget*) override
    {
        p->setRenderHint(QPainter::Antialiasing);

        // 1. Dark housing with rounded corners
        p->setBrush(QColor(25, 25, 25));
        p->setPen(QPen(QColor(90, 90, 90), 1.5));
        p->drawRoundedRect(QRectF(0.5, 0.5, W - 1, H - 1), 4, 4);

        // 2. Lens — circle on the car-facing face
        //    Housing is W wide x H tall.
        //    lens diameter = W - 2*PAD, centred horizontally.
        //    Vertically placed near the open face.
        qreal lensD = W - 2 * PAD;
        qreal lensR = lensD / 2.0;
        QPointF c;   // lens centre

        switch (m_dir) {
        case 0: c = QPointF(W / 2.0,     H - PAD - lensR); break; // bottom face
        case 1: c = QPointF(PAD + lensR, H / 2.0        ); break; // left   face  — but housing is rotated so…
        case 2: c = QPointF(W / 2.0,     PAD + lensR    ); break; // top    face
        case 3: c = QPointF(W - PAD - lensR, H / 2.0   ); break; // right  face
        }

        // Glow
        QColor glow = m_color; glow.setAlpha(50);
        p->setBrush(glow); p->setPen(Qt::NoPen);
        p->drawEllipse(c, lensR + 4, lensR + 4);

        // Lens body
        p->setBrush(m_color);
        p->setPen(QPen(m_color.darker(160), 1));
        p->drawEllipse(c, lensR, lensR);

        // Highlight
        p->setBrush(QColor(255, 255, 255, 90));
        p->setPen(Qt::NoPen);
        p->drawEllipse(c - QPointF(lensR * 0.28, lensR * 0.28),
                       lensR * 0.38, lensR * 0.38);

        // 3. Small direction arrow on the lens face edge
        p->setBrush(QColor(220, 220, 220, 200));
        p->setPen(Qt::NoPen);
        const qreal aw = 4, ah = 3;
        QPolygonF arrow;
        switch (m_dir) {
        case 0: // arrow tip points DOWN
            arrow << QPointF(W/2,      H - 1.5)
                  << QPointF(W/2 - aw, H - 1.5 - ah)
                  << QPointF(W/2 + aw, H - 1.5 - ah);
            break;
        case 1: // arrow tip points LEFT
            arrow << QPointF(1.5,      H/2)
                  << QPointF(1.5 + ah, H/2 - aw)
                  << QPointF(1.5 + ah, H/2 + aw);
            break;
        case 2: // arrow tip points UP
            arrow << QPointF(W/2,      1.5)
                  << QPointF(W/2 - aw, 1.5 + ah)
                  << QPointF(W/2 + aw, 1.5 + ah);
            break;
        case 3: // arrow tip points RIGHT
            arrow << QPointF(W - 1.5,      H/2)
                  << QPointF(W - 1.5 - ah, H/2 - aw)
                  << QPointF(W - 1.5 - ah, H/2 + aw);
            break;
        }
        p->drawPolygon(arrow);
    }

private:
    int    m_dir;
    QColor m_color;

    // Housing dimensions — square so it looks the same regardless of rotation
    static constexpr qreal W   = 22;
    static constexpr qreal H   = 22;
    static constexpr qreal PAD = 3;
};

#endif
