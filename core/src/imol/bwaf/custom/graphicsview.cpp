#include "bwaf/custom/graphicsview.h"

#include <veCommon>

#include <QGraphicsItem>
#include <QKeyEvent>
#include <QScrollBar>
#include <QDebug>

#define TRANSLATE_STEP 2

using namespace bwaf;

const double zoom_min = 0.02;
const double zoom_max = 20;

struct GraphicsView::Data
{
    qreal scale;
    qreal zoom_delta;
    QRectF boundry;
    QPointF click_pos;

    Data() : scale(1.0), zoom_delta(0.1) {}
};

GraphicsView::GraphicsView(QWidget *parent) : QGraphicsView(parent), d(new Data)
{
    setRenderHint(QPainter::Antialiasing);
}

GraphicsView::GraphicsView(QGraphicsScene *scene, QWidget *parent) : QGraphicsView(scene, parent)
{
    setRenderHint(QPainter::Antialiasing);
}

GraphicsView::~GraphicsView()
{
    delete d;
}

qreal GraphicsView::zoomDelta() const { return d->zoom_delta; }
void GraphicsView::setZoomDelta(qreal delta) { d->zoom_delta = delta; }
QRectF GraphicsView::boundry() const { return d->boundry; }
void GraphicsView::setBoundry(const QRectF &boundry) { d->boundry = boundry; }

void GraphicsView::translate(const QPointF &delta)
{
    QRectF new_scene_rect = sceneRect().translated(delta.x(), delta.y());
    setSceneRect((!d->boundry.isEmpty() && !d->boundry.contains(new_scene_rect)) ? d->boundry : new_scene_rect);
}

void GraphicsView::zoom(qreal ratio)
{
    qreal factor = transform().scale(1 + ratio, 1 + ratio).mapRect(QRectF(0, 0, 1, 1)).width();
    if (factor < zoom_min || factor > zoom_max) return;
    zoomTo(d->scale * (1 + ratio));
}

void GraphicsView::zoomTo(qreal ratio)
{
    scale(ratio / d->scale, ratio / d->scale);
    d->scale = ratio;
    emit scaleChanged(d->scale);
}

void GraphicsView::zoomIn() { zoom(d->zoom_delta); }
void GraphicsView::zoomOut() { zoom(-d->zoom_delta); }

void GraphicsView::keyPressEvent(QKeyEvent *event)
{
    switch (event->key()) {
    case Qt::Key_Shift: setDragMode(QGraphicsView::RubberBandDrag); break;
//    case Qt::Key_Up: translate(QPoint(0, -TRANSLATE_STEP)); break;
//    case Qt::Key_Down: translate(QPoint(0, TRANSLATE_STEP)); break;
//    case Qt::Key_Left: translate(QPoint(-TRANSLATE_STEP, 0)); break;
//    case Qt::Key_Right: translate(QPoint(TRANSLATE_STEP, 0)); break;
//    case Qt::Key_Plus: zoomIn(); break;
//    case Qt::Key_Minus: zoomOut(); break;
//    case Qt::Key_Space: rotate(10); break;
    default: break;
    }
    QGraphicsView::keyPressEvent(event);
}

void GraphicsView::keyReleaseEvent(QKeyEvent *event)
{
    switch (event->key()) {
    case Qt::Key_Shift: setDragMode(QGraphicsView::ScrollHandDrag); break;
    default: break;
    }
    QGraphicsView::keyReleaseEvent(event);
}

void GraphicsView::wheelEvent(QWheelEvent *event)
{
    if (event->modifiers() == Qt::ControlModifier) {
        event->angleDelta().y() > 0 ? zoomIn() : zoomOut();
        event->ignore();
    } else {
        QGraphicsView::wheelEvent(event);
    }
}

void GraphicsView::mousePressEvent(QMouseEvent *event)
{
    QGraphicsView::mousePressEvent(event);

    if (event->button() == Qt::LeftButton) d->click_pos = mapToScene(event->pos());
}

void GraphicsView::mouseMoveEvent(QMouseEvent *event)
{
    QGraphicsView::mouseMoveEvent(event);

    if (scene()->mouseGrabberItem() || event->buttons() != Qt::LeftButton || !property("dragable").toBool()) return;

    // Make sure shift is not being pressed
    if ((event->modifiers() & Qt::ShiftModifier) != 0) return;

    translate(d->click_pos - mapToScene(event->pos()));
}

void GraphicsView::mouseReleaseEvent(QMouseEvent *event)
{
    QGraphicsView::mouseReleaseEvent(event);
}
