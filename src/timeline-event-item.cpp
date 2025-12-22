#include "timeline-event-item.h"
#include <QGraphicsSceneMouseEvent>
#include <QGraphicsSceneContextMenuEvent>
#include <QMenu>
#include <QCursor>
#include <QPainter>
#include <QStyleOptionGraphicsItem>
#include <QFontMetrics>

TimelineEventItem::TimelineEventItem(double timeMs, double pixelsPerSecond, TimelineEventType type,
                                     QColor color, QGraphicsItem *parent)
    : QObject(nullptr), QGraphicsEllipseItem(parent), pixelsPerSecond(pixelsPerSecond), 
      eventColor(color), eventType(type)
{
    setRect(0, 0, ITEM_SIZE, ITEM_SIZE);
    setPos(timeMs / 1000.0 * pixelsPerSecond, baseY);
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

void TimelineEventItem::setStackOffset(int offset) {
    stackOffset = offset;
    setPos(x(), baseY + (stackOffset * (ITEM_SIZE + 4)));
}

QString TimelineEventItem::getTypeIcon() const {
    switch (eventType) {
        case TimelineEventType::SceneSwitch:   return "🎬";
        case TimelineEventType::SourceShow:    return "👁";
        case TimelineEventType::SourceHide:    return "🚫";
        case TimelineEventType::MediaStart:    return "▶";
        case TimelineEventType::MediaStop:     return "⏹";
        case TimelineEventType::FilterEnable:  return "✓";
        case TimelineEventType::FilterDisable: return "✗";
        case TimelineEventType::HotkeyTrigger: return "⌨";
        default: return "?";
    }
}

void TimelineEventItem::mouseReleaseEvent(QGraphicsSceneMouseEvent *event) {
    QGraphicsEllipseItem::mouseReleaseEvent(event);
    setCursor(Qt::OpenHandCursor);
    emit eventMoved(getTimeMs());
}

void TimelineEventItem::mouseDoubleClickEvent(QGraphicsSceneMouseEvent *event) {
    QGraphicsEllipseItem::mouseDoubleClickEvent(event);
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
    
    QRectF r = rect();
    
    if (isSelected()) {
        // Selection glow
        painter->setOpacity(0.4);
        painter->setPen(QPen(QColor(255, 200, 0), 5));
        painter->setBrush(Qt::NoBrush);
        painter->drawEllipse(r.adjusted(-3, -3, 3, 3));
        painter->setOpacity(1.0);
        
        painter->setPen(QPen(QColor(255, 200, 0), 2));
        painter->setBrush(QBrush(eventColor.lighter(120)));
    } else {
        painter->setPen(QPen(Qt::black, 1.5));
        painter->setBrush(QBrush(eventColor));
    }
    
    // Draw circle
    painter->drawEllipse(r);
    
    // Draw icon
    int brightness = (eventColor.red() * 299 + eventColor.green() * 587 + eventColor.blue() * 114) / 1000;
    QColor textColor = (brightness > 128) ? Qt::black : Qt::white;
    
    painter->setPen(textColor);
    QFont font = painter->font();
    font.setPointSize(12);
    painter->setFont(font);
    
    QString icon = getTypeIcon();
    painter->drawText(r, Qt::AlignCenter, icon);
}

void TimelineEventItem::setSnapEnabled(bool enabled) {
    snapEnabled = enabled;
}

void TimelineEventItem::setSnapInterval(double intervalMs) {
    snapIntervalMs = intervalMs;
}

QVariant TimelineEventItem::itemChange(GraphicsItemChange change, const QVariant &value) {
    if (change == ItemPositionChange && scene()) {
        QPointF newPos = value.toPointF();
        // Keep Y at stacked position
        newPos.setY(baseY + (stackOffset * (ITEM_SIZE + 4)));
        // Constrain X to positive
        if (newPos.x() < 0) newPos.setX(0);
        
        // Snap to grid if enabled
        if (snapEnabled && snapIntervalMs > 0) {
            double timeMs = newPos.x() / pixelsPerSecond * 1000.0;
            double snappedTimeMs = std::round(timeMs / snapIntervalMs) * snapIntervalMs;
            newPos.setX(snappedTimeMs / 1000.0 * pixelsPerSecond);
        }
        
        return newPos;
    }
    return QGraphicsEllipseItem::itemChange(change, value);
}
