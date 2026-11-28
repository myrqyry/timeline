#include "macro-button-widget.h"
#include "theme-helpers.h"

#include <QPainter>
#include <QFontMetrics>
#include <QFont>
#include <QPalette>
#include <QApplication>
#include <QMouseEvent>
#include <QFile>

static QString fallbackLabel(const MacroButton &b)
{
	if (!b.label.isEmpty())
		return b.label;
	return b.sourceName.isEmpty() ? ThemeIcons::typeDisplayName(b.type) : b.sourceName;
}

MacroButtonWidget::MacroButtonWidget(const MacroButton &button, QWidget *parent) : QWidget(parent), m_button(button)
{
	setMouseTracking(true);
	setAttribute(Qt::WA_Hover, true);
	setCursor(Qt::PointingHandCursor);
	setToolTip(fallbackLabel(m_button) +
		   (m_button.bottomLabel.isEmpty() ? QString() : "\n" + m_button.bottomLabel));
	rebuildRenderer();
	m_bottomHeight = m_button.hasBottomCaption() ? kBottomHeight : 0;
}

void MacroButtonWidget::setButton(const MacroButton &button)
{
	m_button = button;
	setToolTip(fallbackLabel(m_button) +
		   (m_button.bottomLabel.isEmpty() ? QString() : "\n" + m_button.bottomLabel));
	rebuildRenderer();
	updateGeometry();
	update();
}

void MacroButtonWidget::rebuildRenderer()
{
	if (m_renderer) {
		m_renderer->deleteLater();
		m_renderer = nullptr;
	}
	const int brightness =
		(m_button.color.red() * 299 + m_button.color.green() * 587 + m_button.color.blue() * 114) / 1000;
	const QString iconColor = (brightness > 128) ? QStringLiteral("black") : QStringLiteral("white");
	if (!m_button.data.isEmpty() && QFile::exists(m_button.data)) {
		m_renderer = new QSvgRenderer(m_button.data, this);
	} else {
		m_renderer = new QSvgRenderer(ThemeIcons::getIconSvg(m_button.type, iconColor), this);
	}
}

void MacroButtonWidget::setPressedAppearance(bool pressed)
{
	m_isPressed = pressed;
	refreshInteractionMotion();
}

QSize MacroButtonWidget::sizeHint() const
{
	QFont labelFont = font();
	labelFont.setPointSize(8);
	labelFont.setBold(true);
	int labelWidth = QFontMetrics(labelFont).horizontalAdvance(fallbackLabel(m_button)) + 8;
	labelFont.setBold(false);
	labelWidth = std::max(labelWidth, QFontMetrics(labelFont).horizontalAdvance(m_button.bottomLabel) + 8);
	// Keep labels readable without making a long macro name dominate the strip.
	return QSize(qBound(static_cast<int>(kBubbleSize) + 12, labelWidth, 160),
		     m_labelHeight + m_controlHeight + m_bottomHeight);
}

void MacroButtonWidget::setRowHeights(int labelHeight, int controlHeight, int bottomHeight)
{
	m_labelHeight = labelHeight;
	m_controlHeight = controlHeight;
	m_bottomHeight = bottomHeight;
	setFixedHeight(labelHeight + controlHeight + bottomHeight);
	updateGeometry();
	update();
}

QSize MacroButtonWidget::minimumSizeHint() const
{
	return sizeHint();
}

void MacroButtonWidget::mousePressEvent(QMouseEvent *event)
{
	if (event->button() == Qt::LeftButton) {
		setPressedAppearance(true);
		setCursor(Qt::ClosedHandCursor);
	}
	QWidget::mousePressEvent(event);
}

void MacroButtonWidget::mouseReleaseEvent(QMouseEvent *event)
{
	const bool wasPressed = m_isPressed;
	setPressedAppearance(false);
	setCursor(Qt::PointingHandCursor);
	if (wasPressed && event->button() == Qt::LeftButton && rect().contains(event->position().toPoint())) {
		emit fireRequested(m_button);
	}
	QWidget::mouseReleaseEvent(event);
}

void MacroButtonWidget::enterEvent(QEnterEvent *event)
{
	m_isHovered = true;
	setCursor(Qt::PointingHandCursor);
	refreshInteractionMotion();
	QWidget::enterEvent(event);
}

void MacroButtonWidget::leaveEvent(QEvent *event)
{
	m_isHovered = false;
	refreshInteractionMotion();
	QWidget::leaveEvent(event);
}

void MacroButtonWidget::refreshInteractionMotion()
{
	motion.to(m_isPressed ? QPointF(-1, 0) : (m_isHovered ? QPointF(3, 1) : QPointF()), isVisible());
}

void MacroButtonWidget::hideEvent(QHideEvent *event)
{
	m_isPressed = false;
	m_isHovered = false;
	motion.to({}, false);
	QWidget::hideEvent(event);
}

void MacroButtonWidget::paintEvent(QPaintEvent *event)
{
	Q_UNUSED(event);
	QPainter painter(this);
	painter.setRenderHint(QPainter::Antialiasing);
	painter.setRenderHint(QPainter::TextAntialiasing);

	const double bubble = kBubbleSize;
	const double bubbleGrowth = motion.value().x();
	const double radius = (bubble + bubbleGrowth) / 2.0;
	const QPointF center(width() / 2.0, m_labelHeight + m_controlHeight / 2.0); // shared control row center

	// Hover glow
	if (motion.value().y() > 0) {
		QColor glow = m_button.color.lighter(150);
		glow.setAlpha(qRound(80 * motion.value().y()));
		painter.setPen(Qt::NoPen);
		painter.setBrush(QBrush(glow));
		painter.drawEllipse(center, radius + 4.0, radius + 4.0);
	}

	// Bubble fill + border
	painter.setBrush(QBrush(m_button.color));
	painter.setPen(QPen(m_borderColor.isValid() ? m_borderColor : palette().color(QPalette::Text), 1.5));
	painter.drawEllipse(center, radius, radius);

	// Icon (use compact padding = 4 px to mirror TimelineEventItem::paint compact path)
	if (m_renderer && m_renderer->isValid()) {
		const double pad = 4.0;
		const QRectF iconRect(center.x() - radius + pad, center.y() - radius + pad, (radius - pad) * 2.0,
				      (radius - pad) * 2.0);
		m_renderer->render(&painter, iconRect);
	}

	// Label pill uses the same caption row and font as live controls.
	const QString label = fallbackLabel(m_button);
	if (!label.isEmpty()) {
		const int padX = 4;
		QFont font = painter.font();
		font.setPointSize(8);
		font.setBold(true);
		painter.setFont(font);
		QFontMetrics fm(font);
		const int textW = fm.horizontalAdvance(label);
		const int boxW = qMin(textW + padX * 2, width());
		const int boxH = m_labelHeight;
		const int boxX = (width() - boxW) / 2;
		const int boxY = 0;

		QColor textColor = m_labelTextColor.isValid() ? m_labelTextColor : palette().color(QPalette::Text);
		QColor bgColor = m_labelBgColor.isValid() ? m_labelBgColor : palette().color(QPalette::Base);

		QRectF box(boxX, boxY, boxW, boxH);
		QColor pillBg = bgColor;
		pillBg.setAlpha(200);
		painter.setPen(Qt::NoPen);
		painter.setBrush(QBrush(pillBg));
		painter.drawRoundedRect(box, 3.0, 3.0);

		painter.setPen(textColor);
		painter.drawText(box, Qt::AlignCenter, fm.elidedText(label, Qt::ElideRight, boxW - padX * 2));
	}
	if (m_bottomHeight > 0 && !m_button.bottomLabel.isEmpty()) {
		QFont font = painter.font();
		font.setPointSize(8);
		font.setBold(false);
		painter.setFont(font);
		QColor ink = palette().color(QPalette::Text);
		ink.setAlpha(190);
		painter.setPen(ink);
		painter.drawText(QRect(2, m_labelHeight + m_controlHeight, width() - 4, m_bottomHeight),
				 Qt::AlignCenter | Qt::TextSingleLine,
				 QFontMetrics(font).elidedText(m_button.bottomLabel, Qt::ElideRight, width() - 4));
	}
}
