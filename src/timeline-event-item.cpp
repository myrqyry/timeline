#include "timeline-event-item.h"
#include <QGraphicsSceneMouseEvent>
#include <QGraphicsSceneContextMenuEvent>
#include <QMenu>
#include <QCursor>
#include <QPainter>
#include <QStyleOptionGraphicsItem>
#include <QFontMetrics>

TimelineEventItem::TimelineEventItem(double timeMs, double pixelsPerSecond, QColor color, QGraphicsItem *parent)
    : QObject(nullptr), QGraphicsRectItem(parent), pixelsPerSecond(pixelsPerSecond), eventColor(color)
{
    setRect(0, 0, 60, 40); // Wider and taller for better visibility
    setPos(timeMs / 1000.0 * pixelsPerSecond, 25);
    setBrush(QBrush(eventColor));
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

void TimelineEventItem::setEventColor(const QColor &color) {
    eventColor = color;
    setBrush(QBrush(eventColor));
    update();
}

void TimelineEventItem::setEventLabel(const QString &label) {
    eventLabel = label;
    update();
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
    QAction *editAction = menu.addAction("Edit Event");
    QAction *colorAction = menu.addAction("Set Color...");
    menu.addSeparator();
    QAction *deleteAction = menu.addAction("Delete Event");
    
    QAction *selected = menu.exec(event->screenPos());
    if (selected == deleteAction) {
        emit deleteRequested();
    } else if (selected == editAction) {
        emit doubleClicked();
    } else if (selected == colorAction) {
        emit colorChangeRequested();
    }
}

void TimelineEventItem::paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) {
    Q_UNUSED(widget);
    Q_UNUSED(option);
    
    painter->setRenderHint(QPainter::Antialiasing);
    
    // Draw rounded rect
    QRectF r = rect();
    
    if (isSelected()) {
        // Selection glow
        painter->setOpacity(0.3);
        painter->setPen(QPen(QColor(255, 200, 0), 6));
        painter->setBrush(Qt::NoBrush);
        painter->drawRoundedRect(r.adjusted(-2, -2, 2, 2), 4, 4);
        painter->setOpacity(1.0);
        
        painter->setPen(QPen(QColor(255, 200, 0), 2));
        painter->setBrush(QBrush(eventColor.lighter(120)));
    } else {
        painter->setPen(QPen(Qt::black, 1));
        painter->setBrush(QBrush(eventColor));
    }
    
    painter->drawRoundedRect(r, 4, 4);
    
    // Draw label text
    if (!eventLabel.isEmpty()) {
        // Determine text color based on background brightness
        int brightness = (eventColor.red() * 299 + eventColor.green() * 587 + eventColor.blue() * 114) / 1000;
        QColor textColor = (brightness > 128) ? Qt::black : Qt::white;
        
        painter->setPen(textColor);
        QFont font = painter->font();
        font.setPointSize(7);
        font.setBold(true);
        painter->setFont(font);
        
        // Elide text if too long
        QFontMetrics fm(font);
        QString elidedText = fm.elidedText(eventLabel, Qt::ElideRight, r.width() - 4);
        painter->drawText(r.adjusted(2, 2, -2, -2), Qt::AlignCenter | Qt::TextWordWrap, elidedText);
    }
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
