#include "timeline-event-item.h"
#include <obs-module.h>
#include <QGraphicsSceneMouseEvent>
#include <QGraphicsSceneHoverEvent>
#include <QGraphicsSceneContextMenuEvent>
#include <QMenu>
#include <QCursor>
#include <QPainter>
#include <QStyleOptionGraphicsItem>
#include <QFontMetrics>
#include <QFile>
#include <QApplication>

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
	setPen(QPen(borderColor));

	setFlags(ItemIsMovable | ItemIsSelectable | ItemSendsGeometryChanges);
	setAcceptHoverEvents(true);
	setTransformOriginPoint(ITEM_SIZE / 2.0, ITEM_SIZE / 2.0);
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
        renderer->deleteLater();
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
	setCursor(Qt::ClosedHandCursor);
	update();
	QGraphicsEllipseItem::mousePressEvent(event);
}

void TimelineEventItem::mouseReleaseEvent(QGraphicsSceneMouseEvent *event) {
	isBeingDragged = false;
	dragStartedEmitted = false;
	setCursor(isHovered ? Qt::PointingHandCursor : Qt::OpenHandCursor);
	update();
	QGraphicsEllipseItem::mouseReleaseEvent(event);
	emit eventMoved(getTimeMs());
}

void TimelineEventItem::mouseDoubleClickEvent(QGraphicsSceneMouseEvent *event) {
    QGraphicsEllipseItem::mouseDoubleClickEvent(event);
    emit doubleClicked();
}

void TimelineEventItem::contextMenuEvent(QGraphicsSceneContextMenuEvent *event) {
    QMenu menu;
    QAction *editAction = menu.addAction(obs_module_text("TimelineDock.EditEventLabel"));
    QAction *colorAction = menu.addAction(obs_module_text("TimelineDock.SetColor"));
    menu.addSeparator();
    QAction *deleteAction = menu.addAction(obs_module_text("TimelineDock.DeleteEventLabel"));
    
    QAction *selected = menu.exec(event->screenPos());
    if (selected == deleteAction) {
        emit deleteRequested();
    } else if (selected == editAction) {
        emit doubleClicked();
    } else if (selected == colorAction) {
        emit colorChangeRequested();
    }
}

void TimelineEventItem::hoverEnterEvent(QGraphicsSceneHoverEvent *event)
{
	isHovered = true;
	setCursor(Qt::PointingHandCursor);
	update();
	QGraphicsEllipseItem::hoverEnterEvent(event);
}

void TimelineEventItem::hoverLeaveEvent(QGraphicsSceneHoverEvent *event)
{
	isHovered = false;
	setCursor(Qt::OpenHandCursor);
	update();
	QGraphicsEllipseItem::hoverLeaveEvent(event);
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
    
    // Bubble adjustments (Hover grows, Press shrinks)
    QRectF bubbleRect = r;
    double bubbleGrowth = 0.0;
    if (isBeingDragged) {
        bubbleRect.adjust(1, 1, -1, -1);
        bubbleGrowth = -1.0;
    } else if (isHovered) {
        bubbleRect.adjust(-3, -3, 3, 3);
        bubbleGrowth = 3.0;
    }

    if (isSelected()) {
        painter->setBrush(Qt::NoBrush);

        // Outer ring: background color for contrast against whatever is behind
        painter->setPen(QPen(backgroundColor, 3));
        painter->drawEllipse(bubbleRect.adjusted(-2, -2, 2, 2));

        // Inner ring: theme highlight color
        painter->setPen(QPen(selectionColor, 2));
        painter->drawEllipse(bubbleRect.adjusted(-2, -2, 2, 2));
    }

	// Hover glow
	if (isHovered) {
		QColor glow = eventColor.lighter(150);
		glow.setAlpha(80);
		painter->setPen(Qt::NoPen);
		painter->setBrush(QBrush(glow));
		painter->drawEllipse(bubbleRect.adjusted(-4, -4, 4, 4));
	}

	// Draw main circle
	painter->setPen(QPen(borderColor, 1.5));
	painter->setBrush(QBrush(eventColor));
	painter->drawEllipse(bubbleRect);
    
     if (renderer && renderer->isValid()) {
         QRectF iconRect = bubbleRect.adjusted(6, 6, -6, -6); // Padding
         renderer->render(painter, iconRect);
     }

     // Draw circular label (top arch) if label is set
     if (!eventLabel.isEmpty()) {
         painter->save();
         painter->setRenderHint(QPainter::TextAntialiasing);
         // Use QApplication palette for theme-aware text color
         QColor textColor = QApplication::palette().color(QPalette::Text);
         painter->setPen(textColor);
         QFont font = painter->font();
         font.setPointSize(8);
         font.setBold(true);
         painter->setFont(font);

         // Translate to center of item
         painter->translate(ITEM_SIZE / 2.0, ITEM_SIZE / 2.0);

         double radius = (ITEM_SIZE / 2.0) + 4.0 + bubbleGrowth; // Keep outside bubble
         int len = eventLabel.length();
         if (len > 0) {
             // Draw text along top arc (curved around the icon)
             double totalArc = 120.0;
             double startAngle = -150.0; // Start from top-left, through top, to top-right
             double step = totalArc / (double)(len - 1);  // Distribute characters evenly

             for (int i = 0; i < len; ++i) {
                 painter->save();
                 double angle = startAngle + (i * step);
                 painter->rotate(angle);
                 QRectF charRect(-5, -radius - 8, 10, 10);
                 painter->drawText(charRect, Qt::AlignCenter, QString(eventLabel[i]));
                 painter->restore();
             }
         }
         painter->restore();
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

void TimelineEventItem::setBorderColor(const QColor &color) {
    borderColor = color;
    setPen(QPen(borderColor, 1.5));
    update();
}

void TimelineEventItem::setSelectionColor(const QColor &color) {
    selectionColor = color;
    update();
}

void TimelineEventItem::setBackgroundColor(const QColor &color) {
    backgroundColor = color;
    update();
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
