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

TimelineEventItem::TimelineEventItem(double timeMs, double pixelsPerSecond, TimelineEventType type, QColor color,
				     const QString &customIconPath, QGraphicsItem *parent)
	: QObject(nullptr),
	  QGraphicsEllipseItem(parent),
	  pixelsPerSecond(pixelsPerSecond),
	  eventColor(color),
	  customIconPath(customIconPath),
	  eventType(type)
{
	setRect(0, 0, mItemSize, mItemSize);
	// Center the item horizontally on the time
	setPos((timeMs / 1000.0 * pixelsPerSecond) - (mItemSize / 2.0), baseY);
	setBrush(QBrush(eventColor));
	setPen(QPen(borderColor));

	setFlags(ItemIsMovable | ItemIsSelectable | ItemSendsGeometryChanges);
	setAcceptHoverEvents(true);
	setTransformOriginPoint(mItemSize / 2.0, mItemSize / 2.0);
	setCursor(Qt::OpenHandCursor);

	refreshRenderer();
}

double TimelineEventItem::getTimeMs() const
{
	// Add half item width to get center X, then convert to time
	return (x() + mItemSize / 2.0) / pixelsPerSecond * 1000.0;
}

QRectF TimelineEventItem::boundingRect() const
{
	double padding = 6.0; // Enough for selection ring
	QRectF baseRect(0, 0, mItemSize, mItemSize);
	QRectF totalRect = baseRect.adjusted(-padding, -padding, padding, padding);

	if (hasDuration && durationMs > 0) {
		double lineLength = (durationMs / 1000.0) * pixelsPerSecond;
		double startX = mItemSize / 2.0;

		// Rect coverage for the duration line + end cap
		// Line goes from center to center + length
		// We need coverage from center (part of baseRect already) to end
		// Add vertical padding for the line thickness/cap
		QRectF lineRect(startX, mItemSize / 2.0 - 10, lineLength + 10, 20);
		totalRect = totalRect.united(lineRect);
	}

	// Account for label pill above the bubble
	if (!eventLabel.isEmpty()) {
		QFont f;
		f.setPointSize(8);
		f.setBold(true);
		QFontMetrics fm(f);
		int boxW = fm.horizontalAdvance(eventLabel) + 8;
		int boxH = fm.ascent() + fm.descent() + 2;
		QRectF labelRect(mItemSize / 2.0 - boxW / 2.0, -boxH - 3.0, boxW, boxH);
		totalRect = totalRect.united(labelRect);
	}

	return totalRect;
}

void TimelineEventItem::setMaxTime(double maxTimeMs)
{
	this->maxTimeMs = maxTimeMs;
}

void TimelineEventItem::setPixelsPerSecond(double pxPerSec)
{
	double currentTime = getTimeMs();
	pixelsPerSecond = pxPerSec;
	// Recalculate position (center aligned)
	setPos(((currentTime / 1000.0) * pixelsPerSecond) - (mItemSize / 2.0), y());
}

void TimelineEventItem::setEventColor(const QColor &color)
{
	eventColor = color;
	refreshRenderer();
	setBrush(QBrush(eventColor));
	update();
}

void TimelineEventItem::setEventType(TimelineEventType type)
{
	if (eventType == type)
		return;
	eventType = type;
	refreshRenderer();
	update();
}

void TimelineEventItem::setCustomIconPath(const QString &path)
{
	if (customIconPath == path)
		return;
	customIconPath = path;
	refreshRenderer();
	update();
}

void TimelineEventItem::setTimeMs(double timeMs)
{
	setPos(((timeMs / 1000.0) * pixelsPerSecond) - (mItemSize / 2.0), y());
}

void TimelineEventItem::refreshRenderer()
{
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

void TimelineEventItem::setEventLabel(const QString &label)
{
	eventLabel = label;
	update();
}

void TimelineEventItem::setStackOffset(int offset)
{
	stackOffset = offset;
	setPos(x(), baseY + (stackOffset * (mItemSize + 4)));
}

void TimelineEventItem::setCompactMode(bool enabled)
{
	if (compactMode == enabled)
		return;
	double currentTime = getTimeMs();
	compactMode = enabled;
	mItemSize = enabled ? COMPACT_SIZE : ITEM_SIZE;

	// Re-anchor the bubble so its center stays at the same time position
	prepareGeometryChange();
	setRect(0, 0, mItemSize, mItemSize);
	setTransformOriginPoint(mItemSize / 2.0, mItemSize / 2.0);
	setPos(((currentTime / 1000.0) * pixelsPerSecond) - (mItemSize / 2.0), y());
	update();
}

void TimelineEventItem::mousePressEvent(QGraphicsSceneMouseEvent *event)
{
	isBeingDragged = true;
	dragStartedEmitted = false;
	setCursor(Qt::ClosedHandCursor);
	update();
	QGraphicsEllipseItem::mousePressEvent(event);
}

void TimelineEventItem::mouseReleaseEvent(QGraphicsSceneMouseEvent *event)
{
	const bool wasDragged = dragStartedEmitted;
	isBeingDragged = false;
	dragStartedEmitted = false;
	setCursor(isHovered ? Qt::PointingHandCursor : Qt::OpenHandCursor);
	update();
	QGraphicsEllipseItem::mouseReleaseEvent(event);
	if (wasDragged) {
		emit eventMoved(getTimeMs());
	}
}

void TimelineEventItem::mouseDoubleClickEvent(QGraphicsSceneMouseEvent *event)
{
	QGraphicsEllipseItem::mouseDoubleClickEvent(event);
	emit doubleClicked();
}

void TimelineEventItem::contextMenuEvent(QGraphicsSceneContextMenuEvent *event)
{
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

void TimelineEventItem::paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget)
{
	Q_UNUSED(widget);
	Q_UNUSED(option);

	painter->setRenderHint(QPainter::Antialiasing);

	QRectF r = rect();

	// Draw duration line (Background)
	if (hasDuration && durationMs > 0) {
		double lineLength = (durationMs / 1000.0) * pixelsPerSecond;
		// Start from center
		double startX = mItemSize / 2.0;

		QPen linePen(eventColor, 4, Qt::SolidLine, Qt::RoundCap);
		painter->setPen(linePen);
		painter->drawLine(QPointF(startX, mItemSize / 2.0), QPointF(startX + lineLength, mItemSize / 2.0));

		// Draw end cap marker
		painter->drawLine(QPointF(startX + lineLength, mItemSize / 2.0 - 5),
				  QPointF(startX + lineLength, mItemSize / 2.0 + 5));
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
		// Scale icon padding with item size so icons stay centered
		double pad = compactMode ? 4.0 : 6.0;
		QRectF iconRect = bubbleRect.adjusted(pad, pad, -pad, -pad);
		renderer->render(painter, iconRect);
	}

	// Draw event label centered above the bubble
	if (!eventLabel.isEmpty()) {
		painter->save();
		painter->setRenderHint(QPainter::TextAntialiasing);

		// Prefer colors pushed from the dock (OBS palette at creation/refresh time).
		// Fall back to QApplication (global) then a safe light-on-dark default.
		QColor textColor = labelTextColor.isValid() ? labelTextColor
							    : QApplication::palette().color(QPalette::Text);
		QColor bgColor = labelBgColor.isValid() ? labelBgColor : QApplication::palette().color(QPalette::Base);
		if (!textColor.isValid() || textColor.lightness() < 40) {
			textColor = QColor(235, 235, 235);
		}
		if (!bgColor.isValid()) {
			bgColor = QColor(40, 40, 40);
		}

		QFont font = painter->font();
		font.setPointSize(8);
		font.setBold(true);
		painter->setFont(font);

		QFontMetrics fm(font);
		const int padX = 4;
		const int padY = 1;
		int textW = fm.horizontalAdvance(eventLabel);
		int textH = fm.ascent() + fm.descent();
		int boxW = textW + padX * 2;
		int boxH = textH + padY * 2;
		int boxX = (int)(mItemSize / 2.0 - boxW / 2.0);
		int boxY = (int)(-boxH - 3.0 + bubbleGrowth);

		// Background pill for legibility against any color
		QRectF box(boxX, boxY, boxW, boxH);
		QColor pillBg = bgColor;
		pillBg.setAlpha(200);
		painter->setPen(Qt::NoPen);
		painter->setBrush(QBrush(pillBg));
		painter->setRenderHint(QPainter::Antialiasing);
		painter->drawRoundedRect(box, 3.0, 3.0);

		// Text
		painter->setRenderHint(QPainter::TextAntialiasing);
		painter->setPen(textColor);
		painter->drawText(box, Qt::AlignCenter, eventLabel);

		painter->restore();
	}
}

void TimelineEventItem::setDuration(double duration)
{
	durationMs = duration;
	update();
}

void TimelineEventItem::setHasDuration(bool enabled)
{
	hasDuration = enabled;
	update();
}

void TimelineEventItem::setSnapEnabled(bool enabled)
{
	snapEnabled = enabled;
}

void TimelineEventItem::setSnapInterval(double intervalMs)
{
	snapIntervalMs = intervalMs;
}

void TimelineEventItem::setEventId(const QString &id)
{
	eventId = id;
}

void TimelineEventItem::setBorderColor(const QColor &color)
{
	borderColor = color;
	setPen(QPen(borderColor, 1.5));
	update();
}

void TimelineEventItem::setSelectionColor(const QColor &color)
{
	selectionColor = color;
	update();
}

void TimelineEventItem::setBackgroundColor(const QColor &color)
{
	backgroundColor = color;
	update();
}

void TimelineEventItem::setLabelColors(const QColor &textColor, const QColor &bgColor)
{
	labelTextColor = textColor;
	labelBgColor = bgColor;
	update();
}

QString TimelineEventItem::getEventId() const
{
	return eventId;
}

QVariant TimelineEventItem::itemChange(GraphicsItemChange change, const QVariant &value)
{
	if (change == ItemPositionChange && scene()) {
		QPointF newPos = value.toPointF();
		// Keep Y at stacked position
		newPos.setY(baseY + (stackOffset * (mItemSize + 4)));

		double minX = -mItemSize / 2.0; // Allow moving slightly left so center can be at 0
		if (newPos.x() < minX)
			newPos.setX(minX);

		// Clamp to max time if set
		if (maxTimeMs >= 0) {
			double maxX = (maxTimeMs / 1000.0) * pixelsPerSecond - mItemSize / 2.0;
			if (newPos.x() > maxX)
				newPos.setX(maxX);
		}

		// Snap to grid if enabled
		if (snapEnabled && snapIntervalMs > 0) {
			// Calculate current time at center
			double centerX = newPos.x() + mItemSize / 2.0;
			double timeMs = centerX / pixelsPerSecond * 1000.0;

			double snappedTimeMs = std::round(timeMs / snapIntervalMs) * snapIntervalMs;

			// Convert back to top-left position
			double snappedCenterX = snappedTimeMs / 1000.0 * pixelsPerSecond;
			newPos.setX(snappedCenterX - mItemSize / 2.0);

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
			double centerX = newPos.x() + mItemSize / 2.0;
			double timeMs = centerX / pixelsPerSecond * 1000.0;
			if (timeMs < 0)
				timeMs = 0;
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
