#ifndef BWAF_GRAPHICSVIEW_H
#define BWAF_GRAPHICSVIEW_H

#include "../bwaf_global.h"

#include <QGraphicsView>

class RectangleItem;

namespace bwaf {
class BWAFSHARED_EXPORT GraphicsView : public QGraphicsView
{
    Q_OBJECT

public:
    explicit GraphicsView(QWidget *parent = nullptr);
    explicit GraphicsView(QGraphicsScene *scene, QWidget *parent = nullptr);
    ~GraphicsView();

    Q_PROPERTY(qreal zoom_delta READ zoomDelta WRITE setZoomDelta)
    qreal zoomDelta() const;
    void setZoomDelta(qreal delta);

    Q_PROPERTY(QRectF boundry READ boundry WRITE setBoundry)
    QRectF boundry() const;
    void setBoundry(const QRectF &boundry);

signals:
    void scaleChanged(qreal);

public slots:
    void translate(const QPointF &delta);
    void zoom(qreal ratio);
    void zoomTo(qreal ratio);

    void zoomIn();
    void zoomOut();

protected:
    void keyPressEvent(QKeyEvent *event) override;
    void keyReleaseEvent(QKeyEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

protected:
    struct Data;
    Data *d;
};
}

#endif // BWAF_GRAPHICSVIEW_H
