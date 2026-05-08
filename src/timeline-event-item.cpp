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
    setRect(0, 0, ITEM_SIZE, ITEM_SIZE);
    // Center the item horizontally on the time
    setPos((timeMs / 1000.0 * pixelsPerSecond) - (ITEM_SIZE / 2.0), baseY);
    setBrush(QBrush(eventColor));
    setPen(QPen(Qt::black));

    setFlags(ItemIsMovable | ItemIsSelectable | ItemSendsGeometryChanges);
    setCursor(Qt::OpenHandCursor);

    refreshRenderer();
}

double TimelineEventItem::getTimeMs() const {
    // Add half item width to get center X, then convert to time
    return (x() + ITEM_SIZE / 2.0) / pixelsPerSecond * 1000.0;
}

QRectF TimelineEventItem::boundingRect() const {
    double padding = 6.0; // Enough for selection ring
    QRectF baseRect(0, 0, ITEM_SIZE, ITEM_SIZE);
    QRectF totalRect = baseRect.adjusted(-padding, -padding, padding, padding);
    
    if (hasDuration && durationMs > 0) {
        double lineLength = (durationMs / 1000.0) * pixelsPerSecond;
        double startX = ITEM_SIZE / 2.0;
        
        // Rect coverage for the duration line + end cap
        // Line goes from center to center + length
        // We need coverage from center (part of baseRect already) to end
        // Add vertical padding for the line thickness/cap
        QRectF lineRect(startX, ITEM_SIZE/2.0 - 10, lineLength + 10, 20);
        totalRect = totalRect.united(lineRect);
    }
    
    return totalRect;
}

void TimelineEventItem::setMaxTime(double maxTimeMs) {
    this->maxTimeMs = maxTimeMs;
}

void TimelineEventItem::setPixelsPerSecond(double pxPerSec) {
    double currentTime = getTimeMs();
    pixelsPerSecond = pxPerSec;
    // Recalculate position (center aligned)
    setPos(((currentTime / 1000.0) * pixelsPerSecond) - (ITEM_SIZE / 2.0), y());
}

void TimelineEventItem::setEventColor(const QColor &color) {
    eventColor = color;
    refreshRenderer();
    setBrush(QBrush(eventColor));
    update();
}

void TimelineEventItem::setEventType(TimelineEventType type) {
    if (eventType == type)
        return;
    eventType = type;
    refreshRenderer();
    update();
}

void TimelineEventItem::setCustomIconPath(const QString &path) {
    if (customIconPath == path)
        return;
    customIconPath = path;
    refreshRenderer();
    update();
}

void TimelineEventItem::setTimeMs(double timeMs) {
    setPos(((timeMs / 1000.0) * pixelsPerSecond) - (ITEM_SIZE / 2.0), y());
}

void TimelineEventItem::refreshRenderer() {
    if (renderer) {
        delete renderer;
        renderer = nullptr;
    }
    int brightness = (eventColor.red() * 299 + eventColor.green() * 587 + eventColor.blue() * 114) / 1000;
    QString iconColor = (brightness > 128) ? "black" : "white";
    if (!customIconPath.isEmpty() && QFile::exists(customIconPath)) {
        renderer = new QSvgRenderer(customIconPath, this);
    } else {
        renderer = new QSvgRenderer(ThemeIcons::getIconSvg(eventType, iconColor), this);
    }
}

void TimelineEventItem::setEventLabel(const QString &label) {
    eventLabel = label;
    update();
}

void TimelineEventItem::setStackOffset(int offset) {
    stackOffset = offset;
    setPos(x(), baseY + (stackOffset * (ITEM_SIZE + 4)));
}



void TimelineEventItem::mousePressEvent(QGraphicsSceneMouseEvent *event) {
    isBeingDragged = true;
    dragStartedEmitted = false;
    QGraphicsEllipseItem::mousePressEvent(event);
}

void TimelineEventItem::mouseReleaseEvent(QGraphicsSceneMouseEvent *event) {
    isBeingDragged = false;
    dragStartedEmitted = false;
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
    
    // Draw duration line (Background)
    if (hasDuration && durationMs > 0) {
        double lineLength = (durationMs / 1000.0) * pixelsPerSecond;
        // Start from center
        double startX = ITEM_SIZE / 2.0;
        
        QPen linePen(eventColor, 4, Qt::SolidLine, Qt::RoundCap);
        painter->setPen(linePen);
        painter->drawLine(QPointF(startX, ITEM_SIZE/2.0), QPointF(startX + lineLength, ITEM_SIZE/2.0));
        
        // Draw end cap marker
        painter->drawLine(QPointF(startX + lineLength, ITEM_SIZE/2.0 - 5), 
                          QPointF(startX + lineLength, ITEM_SIZE/2.0 + 5));
    }
    
    if (isSelected()) {
        // Cleaner selection ring
        painter->setBrush(Qt::NoBrush);
        
        // White outer ring for contrast
        painter->setPen(QPen(Qt::white, 3));
        painter->drawEllipse(r.adjusted(-2, -2, 2, 2));
        
        // Color inner ring
        painter->setPen(QPen(QColor(255, 200, 0), 2)); // Gold/Orange
        painter->drawEllipse(r.adjusted(-2, -2, 2, 2));
    } else {
        painter->setPen(QPen(Qt::black, 1.5));
        painter->setBrush(QBrush(eventColor));
    }
    
    // Draw circle
    painter->drawEllipse(r);
    
    if (renderer && renderer->isValid()) {
        QRectF iconRect = r.adjusted(6, 6, -6, -6); // Padding
        renderer->render(painter, iconRect);
    }
}

void TimelineEventItem::setDuration(double duration) {
    durationMs = duration;
    update();
}

void TimelineEventItem::setHasDuration(bool enabled) {
    hasDuration = enabled;
    update();
}

void TimelineEventItem::setSnapEnabled(bool enabled) {
    snapEnabled = enabled;
}

void TimelineEventItem::setSnapInterval(double intervalMs) {
    snapIntervalMs = intervalMs;
}

void TimelineEventItem::setEventId(const QString &id) {
    eventId = id;
}

QString TimelineEventItem::getEventId() const {
    return eventId;
}

QVariant TimelineEventItem::itemChange(GraphicsItemChange change, const QVariant &value) {
    if (change == ItemPositionChange && scene()) {
        QPointF newPos = value.toPointF();
        // Keep Y at stacked position
        newPos.setY(baseY + (stackOffset * (ITEM_SIZE + 4)));
        
        double minX = -ITEM_SIZE / 2.0; // Allow moving slightly left so center can be at 0
        if (newPos.x() < minX) newPos.setX(minX);

        // Clamp to max time if set
        if (maxTimeMs >= 0) {
            double maxX = (maxTimeMs / 1000.0) * pixelsPerSecond - ITEM_SIZE / 2.0;
            if (newPos.x() > maxX) newPos.setX(maxX);
        }

        // Snap to grid if enabled
        if (snapEnabled && snapIntervalMs > 0) {
            // Calculate current time at center
            double centerX = newPos.x() + ITEM_SIZE / 2.0;
            double timeMs = centerX / pixelsPerSecond * 1000.0;

            double snappedTimeMs = std::round(timeMs / snapIntervalMs) * snapIntervalMs;

            // Convert back to top-left position
            double snappedCenterX = snappedTimeMs / 1000.0 * pixelsPerSecond;
            newPos.setX(snappedCenterX - ITEM_SIZE / 2.0);

            // Emit drag signals only during actual user drag
            if (isBeingDragged && !suppressDragSignals) {
                if (!dragStartedEmitted) {
                    dragStartedEmitted = true;
                    emit dragStarted();
                }
                emit dragMoved(snappedTimeMs);
            }
        } else {
            // Calculate time for current pos
            double centerX = newPos.x() + ITEM_SIZE / 2.0;
            double timeMs = centerX / pixelsPerSecond * 1000.0;
            if (timeMs < 0) timeMs = 0;
            if (isBeingDragged && !suppressDragSignals) {
                if (!dragStartedEmitted) {
                    dragStartedEmitted = true;
                    emit dragStarted();
                }
                emit dragMoved(timeMs);
            }
        }
        
        return newPos;
    }
    return QGraphicsEllipseItem::itemChange(change, value);
}
