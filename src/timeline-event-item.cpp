#include "timeline-event-item.h"
#include <obs-module.h>
#include <QGraphicsSceneMouseEvent>
#include <QGraphicsScene>
#include <QGraphicsSceneHoverEvent>
#include <QGraphicsSceneContextMenuEvent>
#include <QMenu>
#include <QCursor>
#include <QPainter>
#include <QPainterPath>
#include <QStyleOptionGraphicsItem>
#include <QFontMetrics>
#include <QFile>
#include <QApplication>
#include <QScopedValueRollback>

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
	setPen(QPen(borderColor, 1.5));

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

double TimelineEventItem::durationHandleX() const
{
	// Keep the grip outside the bubble even at extremely short durations.
	return std::max(mItemSize + 10.0, mItemSize / 2.0 + durationMs * pixelsPerSecond / 1000.0);
}

bool TimelineEventItem::durationHandleContains(const QPointF &scenePoint) const
{
	if (!hasDuration || durationMs <= 0.0)
		return false;
	const auto p = mapFromScene(scenePoint);
	return std::abs(p.x() - durationHandleX()) <= 8.0 && std::abs(p.y() - mItemSize / 2.0) <= 10.0;
}

QRectF TimelineEventItem::boundingRect() const
{
	const double padding = VISUAL_PADDING; // Selection ring and hover glow
	QRectF baseRect(0, 0, mItemSize, mItemSize);
	QRectF totalRect = baseRect.adjusted(-padding, -padding, padding, padding);

	if (hasDuration && durationMs > 0) {
		double startX = mItemSize / 2.0;

		// Rect coverage for the duration line + end cap
		// Line goes from center to center + length
		// We need coverage from center (part of baseRect already) to end
		// Add vertical padding for the line thickness/cap
		QRectF lineRect(startX, -8, durationHandleX() - startX + 10, mItemSize + 28);
		totalRect = totalRect.united(lineRect);
	}

	// Account for label pill above the bubble
	if (showLabel && !eventLabel.isEmpty()) {
		QFont f;
		f.setPointSize(8);
		f.setBold(true);
		QFontMetrics fm(f);
		int boxW = fm.horizontalAdvance(eventLabel) + 8;
		int boxH = fm.ascent() + fm.descent() + 2;
		QRectF labelRect(mItemSize / 2.0 - boxW / 2.0, -boxH - 4.0, boxW, boxH + 4.0);
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
	if (pixelsPerSecond == pxPerSec)
		return;
	const QScopedValueRollback<bool> layoutChange(suppressDragSignals, true);
	double currentTime = getTimeMs();
	prepareGeometryChange();
	pixelsPerSecond = pxPerSec;
	// Recalculate position (center aligned)
	setPos(((currentTime / 1000.0) * pixelsPerSecond) - (mItemSize / 2.0), y());
}

void TimelineEventItem::setEventColor(const QColor &color)
{
	if (eventColor == color)
		return;
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
	const QScopedValueRollback<bool> layoutChange(suppressDragSignals, true);
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
	if (eventLabel == label)
		return;
	prepareGeometryChange();
	eventLabel = label;
	update();
}

void TimelineEventItem::setStackOffset(int offset)
{
	if (stackOffset == offset && y() == baseY + offset * (mItemSize + 4))
		return;
	const QScopedValueRollback<bool> layoutChange(suppressDragSignals, true);
	stackOffset = offset;
	setPos(x(), baseY + (stackOffset * (mItemSize + 4)));
}

void TimelineEventItem::setCompactMode(bool enabled)
{
	const QScopedValueRollback<bool> layoutChange(suppressDragSignals, true);
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
	refreshInteractionMotion();
	QGraphicsEllipseItem::mousePressEvent(event);
}

void TimelineEventItem::mouseReleaseEvent(QGraphicsSceneMouseEvent *event)
{
	const bool wasDragged = dragStartedEmitted;
	isBeingDragged = false;
	dragStartedEmitted = false;
	setCursor(isHovered ? Qt::PointingHandCursor : Qt::OpenHandCursor);
	refreshInteractionMotion();
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
	auto *muteAction = menu.addAction(obs_module_text(muted ? "TimelineDock.UnmuteCue" : "TimelineDock.MuteCue"));
	muteAction->setObjectName("timeline-cue-mute-action");
	muteAction->setEnabled(cueMenuAvailability && cueMenuAvailability(true));
	auto *runAction =
		menu.addAction(obs_module_text(hasDuration ? "TimelineDock.RunCueStart" : "TimelineDock.RunCueNow"));
	runAction->setObjectName("timeline-cue-run-start-action");
	runAction->setEnabled(manualCanRun && !muted && cueMenuAvailability && cueMenuAvailability(false));
	QAction *endAction = nullptr;
	if (hasDuration && manualCanEnd) {
		endAction = menu.addAction(obs_module_text("TimelineDock.RunCueEnd"));
		endAction->setObjectName("timeline-cue-run-end-action");
		endAction->setEnabled(!muted && cueMenuAvailability && cueMenuAvailability(false));
	}
	menu.addSeparator();
	QAction *deleteAction = menu.addAction(obs_module_text("TimelineDock.DeleteEventLabel"));

	QAction *selected = menu.exec(event->screenPos());
	if (!selected)
		return;
	if (selected == muteAction) {
		emit muteRequested();
	} else if (selected == runAction || selected == endAction) {
		emit manualExecutionRequested(selected == endAction);
	} else if (selected == deleteAction) {
		emit deleteRequested();
	} else if (selected == editAction) {
		emit doubleClicked();
	} else if (selected == colorAction) {
		emit colorChangeRequested();
	}
}

void TimelineEventItem::refreshInteractionMotion()
{
	motion.to(isBeingDragged ? QPointF(-1, 0) : (isHovered ? QPointF(3, 1) : QPointF()),
		  isVisible() && scene() && !scene()->views().isEmpty());
}

void TimelineEventItem::hoverEnterEvent(QGraphicsSceneHoverEvent *event)
{
	isHovered = true;
	setCursor(Qt::PointingHandCursor);
	refreshInteractionMotion();
	QGraphicsEllipseItem::hoverEnterEvent(event);
}

void TimelineEventItem::hoverLeaveEvent(QGraphicsSceneHoverEvent *event)
{
	isHovered = false;
	setCursor(Qt::OpenHandCursor);
	refreshInteractionMotion();
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
		if (rampSamples && lineLength >= mItemSize) {
			// Normalized shape sampled from playback's exact easing function.
			// Constant-size sample storage avoids work proportional to duration.
			QPainterPath path;
			for (size_t i = 0; i < rampSamples->size(); ++i) {
				const double x = startX + lineLength * i / (rampSamples->size() - 1);
				const double y = mItemSize / 2.0 + 7.0 - 14.0 * (*rampSamples)[i];
				if (i == 0)
					path.moveTo(x, y);
				else
					path.lineTo(x, y);
			}
			painter->setPen(QPen(eventColor, 2.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
			painter->drawPath(path);
		} else {
			painter->drawLine(QPointF(startX, mItemSize / 2.0),
					  QPointF(startX + lineLength, mItemSize / 2.0));
		}

		// Draw a visible grip; short spans use a small dashed leader to
		// avoid covering the cue bubble itself.
		const double handleX = durationHandleX();
		if (handleX > startX + lineLength) {
			painter->setPen(QPen(eventColor, 1, Qt::DotLine));
			painter->drawLine(QPointF(startX + lineLength, mItemSize / 2.0),
					  QPointF(handleX, mItemSize / 2.0));
		}
		painter->setPen(QPen(eventColor, 3, Qt::SolidLine, Qt::RoundCap));
		painter->drawLine(QPointF(handleX, mItemSize / 2.0 - 6), QPointF(handleX, mItemSize / 2.0 + 6));

		// Only display a label when it fits between the bubble and the grip.
		// The pill sits above the line (below for ramps, to preserve the curve).
		painter->save();
		QFont font = painter->font();
		font.setPointSize(8);
		painter->setFont(font);
		const QString text = durationLabel();
		const QFontMetricsF metrics(font);
		const double width = metrics.horizontalAdvance(text) + 8.0;
		const double height = metrics.height() + 2.0;
		const double left = mItemSize + 6.0;
		const double right = startX + lineLength - 9.0;
		if (right - left >= width && height <= mItemSize + 4.0) {
			const QRectF pill((left + right - width) / 2.0,
					  rampSamples ? mItemSize / 2.0 + 8.0 : mItemSize / 2.0 - height - 2.0, width,
					  height);
			painter->setPen(Qt::NoPen);
			painter->setBrush(labelBgColor.isValid() ? labelBgColor : backgroundColor);
			painter->drawRoundedRect(pill, 3, 3);
			painter->setPen(labelTextColor.isValid() ? labelTextColor : borderColor);
			painter->drawText(pill, Qt::AlignCenter, text);
		}
		painter->restore();
	}

	// Bubble adjustments (Hover grows, Press shrinks)
	const double bubbleGrowth = motion.value().x();
	const QRectF bubbleRect = r.adjusted(-bubbleGrowth, -bubbleGrowth, bubbleGrowth, bubbleGrowth);

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
	if (motion.value().y() > 0) {
		QColor glow = eventColor.lighter(150);
		glow.setAlpha(qRound(80 * motion.value().y()));
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

	// A small status mark stays within the existing selection-ring bounds.
	if (targetMissing) {
		const QPointF center(mItemSize - 2, 2);
		painter->setPen(QPen(backgroundColor, 1.5));
		painter->setBrush(QColor("#e5a43b"));
		painter->drawEllipse(center, 6.0, 6.0);
		painter->setPen(QPen(QColor("#21180c"), 1.5, Qt::SolidLine, Qt::RoundCap));
		painter->drawLine(center + QPointF(0, -3), center + QPointF(0, 1));
		painter->drawPoint(center + QPointF(0, 3));
	}

	// Draw event label centered above the bubble
	if (showLabel && !eventLabel.isEmpty()) {
		painter->save();
		painter->setRenderHint(QPainter::TextAntialiasing);

		// The dock supplies the current palette; valid dark text is not an error.
		QColor textColor = labelTextColor.isValid() ? labelTextColor
							    : QApplication::palette().color(QPalette::Text);
		QColor bgColor = labelBgColor.isValid() ? labelBgColor : QApplication::palette().color(QPalette::Base);

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
		int boxY = (int)(-boxH - 3.0);

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

void TimelineEventItem::setRampVisualization(const std::optional<MacroParameter::Ramp> &ramp)
{
	if (!ramp) {
		if (rampSamples) {
			rampSamples.reset();
			update();
		}
		return;
	}
	const double start = ramp->start.value.toDouble();
	const int direction = ramp->end > start ? 1 : (ramp->end < start ? -1 : 0);
	if (rampSamples && rampEasing == ramp->easing && rampDirection == direction)
		return;
	rampDirection = direction;
	rampEasing = ramp->easing;
	auto normalized = *ramp;
	normalized.start.value = direction < 0 ? 1.0 : (direction == 0 ? 0.5 : 0.0);
	normalized.end = direction > 0 ? 1.0 : (direction == 0 ? 0.5 : 0.0);
	rampSamples.emplace();
	for (size_t i = 0; i < rampSamples->size(); ++i)
		(*rampSamples)[i] = MacroParameter::rampValue(normalized, double(i) / (rampSamples->size() - 1));
	update();
}

QString TimelineEventItem::durationLabel() const
{
	if (durationMs < 1000.0)
		return QString::number(durationMs, 'g', 4) + QStringLiteral(" ms");
	return QString::number(durationMs / 1000.0, 'g', 5) + QStringLiteral(" s");
}

void TimelineEventItem::setCueTooltip(const QString &text)
{
	if (cueTooltip == text)
		return;
	cueTooltip = text;
	refreshCueTooltip();
}

void TimelineEventItem::setTargetMissing(bool missing)
{
	if (targetMissing == missing)
		return;
	targetMissing = missing;
	refreshCueTooltip();
	update();
}

void TimelineEventItem::refreshCueTooltip()
{
	setToolTip(targetMissing ? QString::fromUtf8(obs_module_text("TimelineDock.TargetMissing")) + "\n" + cueTooltip
				 : cueTooltip);
}

void TimelineEventItem::setMuted(bool value)
{
	if (muted == value)
		return;
	muted = value;
	setOpacity(muted ? 0.45 : 1.0);
	update();
}

void TimelineEventItem::setDuration(double duration)
{
	if (durationMs == duration)
		return;
	prepareGeometryChange();
	durationMs = duration;
	update();
}

void TimelineEventItem::setHasDuration(bool enabled)
{
	if (hasDuration == enabled)
		return;
	prepareGeometryChange();
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
	if (borderColor == color)
		return;
	borderColor = color;
	setPen(QPen(borderColor, 1.5));
	update();
}

void TimelineEventItem::setSelectionColor(const QColor &color)
{
	if (selectionColor == color)
		return;
	selectionColor = color;
	update();
}

void TimelineEventItem::setBackgroundColor(const QColor &color)
{
	if (backgroundColor == color)
		return;
	backgroundColor = color;
	update();
}

void TimelineEventItem::setLabelColors(const QColor &textColor, const QColor &bgColor)
{
	if (labelTextColor == textColor && labelBgColor == bgColor)
		return;
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
	if (change == ItemVisibleHasChanged && !value.toBool()) {
		isHovered = false;
		motion.to({}, false);
	}
	if (change == ItemPositionChange && scene()) {
		QPointF newPos = value.toPointF();
		// Keep Y at stacked position
		newPos.setY(baseY + (stackOffset * (mItemSize + 4)));

		// Snapping is an editing gesture, not a layout operation. Zoom,
		// compact mode, stacking and model updates must preserve exact time.
		const bool userDrag = isBeingDragged && !suppressDragSignals;
		if (userDrag && snapEnabled && snapIntervalMs > 0) {
			const double timeMs = (newPos.x() + mItemSize / 2.0) / pixelsPerSecond * 1000.0;
			const double snapped = std::round(timeMs / snapIntervalMs) * snapIntervalMs;
			newPos.setX(snapped / 1000.0 * pixelsPerSecond - mItemSize / 2.0);
		}

		// Model/layout restores must show exact saved positions, including cues
		// intentionally placed beyond an end marker. Only constrain movement.
		if (!suppressDragSignals) {
			const double minX = -mItemSize / 2.0;
			if (newPos.x() < minX)
				newPos.setX(minX);

			if (maxTimeMs >= 0) {
				const double maxX = maxTimeMs / 1000.0 * pixelsPerSecond - mItemSize / 2.0;
				if (newPos.x() > maxX)
					newPos.setX(maxX);
			}
		}

		// The guide and committed position must agree, even at the end marker.
		if (userDrag) {
			if (!dragStartedEmitted) {
				dragStartedEmitted = true;
				emit dragStarted();
			}
			emit dragMoved((newPos.x() + mItemSize / 2.0) / pixelsPerSecond * 1000.0);
		}

		return newPos;
	}
	return QGraphicsEllipseItem::itemChange(change, value);
}
