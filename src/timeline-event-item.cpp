#include "timeline-event-item.h"
#include <QGraphicsSceneMouseEvent>
#include <QGraphicsSceneContextMenuEvent>
#include <QMenu>
#include <QCursor>
#include <QPainter>
#include <QStyleOptionGraphicsItem>

TimelineEventItem::TimelineEventItem(double timeMs, double pixelsPerSecond, QGraphicsItem *parent)
    : QObject(nullptr), QGraphicsRectItem(parent), pixelsPerSecond(pixelsPerSecond)
{
    setRect(0, 0, 10, 30);
    setPos(timeMs / 1000.0 * pixelsPerSecond, 25);
    setBrush(QBrush(QColor(0, 200, 255)));
    setPen(QPen(Qt::black));
    
    setFlags(ItemIsMovable | ItemIsSelectable | ItemSendsGeometryChanges);
    setCursor(Qt::OpenHandCursor);
}

double TimelineEventItem::getTimeMs() const {
    return x() / pixelsPerSecond * 1000.0;
}

void TimelineEventItem::setPixelsPerSecond(double pxPerSec) {
    double currentTime = getTimeMs();
    pixelsPerSecond = pxPerSec;
    setPos((currentTime / 1000.0) * pixelsPerSecond, y());
}

void TimelineEventItem::mouseReleaseEvent(QGraphicsSceneMouseEvent *event) {
    QGraphicsRectItem::mouseReleaseEvent(event);
    setCursor(Qt::OpenHandCursor);
    emit eventMoved(getTimeMs());
}

void TimelineEventItem::mouseDoubleClickEvent(QGraphicsSceneMouseEvent *event) {
    QGraphicsRectItem::mouseDoubleClickEvent(event);
    emit doubleClicked();
}

void TimelineEventItem::contextMenuEvent(QGraphicsSceneContextMenuEvent *event) {
    QMenu menu;
    QAction *deleteAction = menu.addAction("Delete Event");
    QAction *editAction = menu.addAction("Edit Event");
    
    QAction *selected = menu.exec(event->screenPos());
    if (selected == deleteAction) {
        emit deleteRequested();
    } else if (selected == editAction) {
        emit doubleClicked();
    }
}

void TimelineEventItem::paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) {
    Q_UNUSED(widget);
    
    // Custom selection appearance
    if (isSelected()) {
        painter->setPen(QPen(QColor(255, 200, 0), 3)); // Gold outline
        painter->setBrush(QBrush(QColor(0, 220, 255))); // Bright cyan
        
        // Draw glow effect
        painter->setOpacity(0.3);
        painter->setPen(QPen(QColor(255, 200, 0), 6));
        painter->drawRect(rect().adjusted(-2, -2, 2, 2));
        painter->setOpacity(1.0);
    } else {
        painter->setPen(QPen(Qt::black, 2));
        painter->setBrush(QBrush(QColor(0, 200, 255)));
    }
    
    painter->drawRect(rect());
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
