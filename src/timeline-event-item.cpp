#include "timeline-event-item.h"
#include <QGraphicsSceneMouseEvent>
#include <QGraphicsSceneContextMenuEvent>
#include <QMenu>
#include <QCursor>
#include <QPainter>
#include <QStyleOptionGraphicsItem>
#include <QFontMetrics>
#include <QFile>

#include "theme-helpers.h"

TimelineEventItem::TimelineEventItem(double timeMs, double pixelsPerSecond, TimelineEventType type,
                                     QColor color, const QString &customIconPath, QGraphicsItem *parent)
    : QObject(nullptr), QGraphicsEllipseItem(parent), pixelsPerSecond(pixelsPerSecond), 
      eventColor(color), customIconPath(customIconPath), eventType(type)
{
    // Determine contrast color
    int brightness = (eventColor.red() * 299 + eventColor.green() * 587 + eventColor.blue() * 114) / 1000;
    QString iconColor = (brightness > 128) ? "black" : "white";

    // Load SVG icon
    if (!customIconPath.isEmpty() && QFile::exists(customIconPath)) {
        renderer = new QSvgRenderer(customIconPath, this);
    } else {
        renderer = new QSvgRenderer(ThemeIcons::getIconSvg(type, iconColor), this);
    }

    setRect(0, 0, ITEM_SIZE, ITEM_SIZE);
    // Center the item horizontally on the time
    setPos((timeMs / 1000.0 * pixelsPerSecond) - (ITEM_SIZE / 2.0), baseY);
    setBrush(QBrush(eventColor));
    setPen(QPen(Qt::black));
    
    setFlags(ItemIsMovable | ItemIsSelectable | ItemSendsGeometryChanges);
    setCursor(Qt::OpenHandCursor);
}

double TimelineEventItem::getTimeMs() const {
    // Add half item width to get center X, then convert to time
    return (x() + ITEM_SIZE / 2.0) / pixelsPerSecond * 1000.0;
}

void TimelineEventItem::setPixelsPerSecond(double pxPerSec) {
    double currentTime = getTimeMs();
    pixelsPerSecond = pxPerSec;
    // Recalculate position (center aligned)
    setPos(((currentTime / 1000.0) * pixelsPerSecond) - (ITEM_SIZE / 2.0), y());
}

void TimelineEventItem::setEventColor(const QColor &color) {
    eventColor = color;
    
    // Update SVG renderer with contrast color
    if (renderer) delete renderer;

    if (!customIconPath.isEmpty() && QFile::exists(customIconPath)) {
        renderer = new QSvgRenderer(customIconPath, this);
    } else {
        int brightness = (eventColor.red() * 299 + eventColor.green() * 587 + eventColor.blue() * 114) / 1000;
        QString iconColor = (brightness > 128) ? "black" : "white";
        renderer = new QSvgRenderer(ThemeIcons::getIconSvg(eventType, iconColor), this);
    }
    
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
    if (renderer && renderer->isValid()) {
        QRectF iconRect = r.adjusted(6, 6, -6, -6); // Padding
        renderer->render(painter, iconRect);
    }
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
        
        double minX = -ITEM_SIZE / 2.0; // Allow moving slightly left so center can be at 0
        if (newPos.x() < minX) newPos.setX(minX);
        
        // Snap to grid if enabled
        if (snapEnabled && snapIntervalMs > 0) {
            // Calculate current time at center
            double centerX = newPos.x() + ITEM_SIZE / 2.0;
            double timeMs = centerX / pixelsPerSecond * 1000.0;
            
            double snappedTimeMs = std::round(timeMs / snapIntervalMs) * snapIntervalMs;
            
            // Convert back to top-left position
            double snappedCenterX = snappedTimeMs / 1000.0 * pixelsPerSecond;
            newPos.setX(snappedCenterX - ITEM_SIZE / 2.0);
        }
        
        return newPos;
    }
    return QGraphicsEllipseItem::itemChange(change, value);
}
