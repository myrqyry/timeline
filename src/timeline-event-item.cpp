#include "timeline-event-item.h"
#include <QGraphicsSceneMouseEvent>
#include <QCursor>

TimelineEventItem::TimelineEventItem(double timeMs, double pixelsPerSecond, QGraphicsItem *parent)
    : QObject(nullptr), QGraphicsRectItem(parent), pixelsPerSecond(pixelsPerSecond), timeMs(timeMs)
{
    setRect(0, 0, 10, 30);
    setPos(timeMs / 1000.0 * pixelsPerSecond, 25);
    setBrush(QBrush(Qt::cyan));
    setPen(QPen(Qt::black));
    
    setFlags(ItemIsMovable | ItemIsSelectable | ItemSendsGeometryChanges);
    setCursor(Qt::OpenHandCursor);
}

double TimelineEventItem::getTimeMs() const {
    return x() / pixelsPerSecond * 1000.0;
}

void TimelineEventItem::setPixelsPerSecond(double pxPerSec) {
    pixelsPerSecond = pxPerSec;
    setPos(timeMs / 1000.0 * pixelsPerSecond, y());
}

void TimelineEventItem::mouseReleaseEvent(QGraphicsSceneMouseEvent *event) {
    QGraphicsRectItem::mouseReleaseEvent(event);
    setCursor(Qt::OpenHandCursor);
    emit eventMoved(getTimeMs());
}

QVariant TimelineEventItem::itemChange(GraphicsItemChange change, const QVariant &value) {
    if (change == ItemPositionChange && scene()) {
        QPointF newPos = value.toPointF();
        // Constrain Y to track height
        newPos.setY(25);
        // Constrain X to positive
        if (newPos.x() < 0) newPos.setX(0);
        return newPos;
    }
    return QGraphicsRectItem::itemChange(change, value);
}
