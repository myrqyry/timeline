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
	rebuildRenderer();
}

void MacroButtonWidget::setButton(const MacroButton &button)
{
	m_button = button;
	setToolTip(fallbackLabel(m_button));
	rebuildRenderer();
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
	update();
}

QSize MacroButtonWidget::sizeHint() const
{
	// Bubble + label pill above + margins.
	return QSize(static_cast<int>(kBubbleSize) + 12, static_cast<int>(kBubbleSize) + 18);
}

QSize MacroButtonWidget::minimumSizeHint() const
{
	return sizeHint();
}

void MacroButtonWidget::mousePressEvent(QMouseEvent *event)
{
	if (event->button() == Qt::LeftButton) {
		m_isPressed = true;
		setCursor(Qt::ClosedHandCursor);
		update();
	}
	QWidget::mousePressEvent(event);
}

void MacroButtonWidget::mouseReleaseEvent(QMouseEvent *event)
{
	const bool wasPressed = m_isPressed;
	m_isPressed = false;
	setCursor(Qt::PointingHandCursor);
	update();
	if (wasPressed && event->button() == Qt::LeftButton && rect().contains(event->position().toPoint())) {
		emit fireRequested(m_button);
	}
	QWidget::mouseReleaseEvent(event);
}

void MacroButtonWidget::enterEvent(QEnterEvent *event)
{
	m_isHovered = true;
	setCursor(Qt::PointingHandCursor);
	update();
	QWidget::enterEvent(event);
}

void MacroButtonWidget::leaveEvent(QEvent *event)
{
	m_isHovered = false;
	update();
	QWidget::leaveEvent(event);
}

void MacroButtonWidget::paintEvent(QPaintEvent *event)
{
	Q_UNUSED(event);
	QPainter painter(this);
	painter.setRenderHint(QPainter::Antialiasing);
	painter.setRenderHint(QPainter::TextAntialiasing);

	const double bubble = kBubbleSize;
	const double bubbleGrowth = m_isPressed ? -1.0 : (m_isHovered ? 3.0 : 0.0);
	const double radius = (bubble + bubbleGrowth) / 2.0;
	const QPointF center(bubble / 2.0 + 4.0, bubble + 4.0); // bubble centered horizontally, anchored to bottom

	// Hover glow
	if (m_isHovered && !m_isPressed) {
		QColor glow = m_button.color.lighter(150);
		glow.setAlpha(80);
		painter.setPen(Qt::NoPen);
		painter.setBrush(QBrush(glow));
		painter.drawEllipse(center, radius + 4.0, radius + 4.0);
	}

	// Bubble fill + border
	painter.setBrush(QBrush(m_button.color));
	painter.setPen(QPen(m_borderColor, 1.5));
	painter.drawEllipse(center, radius, radius);

	// Icon (use compact padding = 4 px to mirror TimelineEventItem::paint compact path)
	if (m_renderer && m_renderer->isValid()) {
		const double pad = 4.0;
		const QRectF iconRect(center.x() - radius + pad, center.y() - radius + pad, (radius - pad) * 2.0,
				      (radius - pad) * 2.0);
		m_renderer->render(&painter, iconRect);
	}

	// Label pill (drawn above bubble; bubble center sits ~4 px above widget bottom so the pill fits)
	const QString label = fallbackLabel(m_button);
	if (!label.isEmpty()) {
		const int padX = 4;
		const int padY = 1;
		QFont font = painter.font();
		font.setPointSize(8);
		font.setBold(true);
		painter.setFont(font);
		QFontMetrics fm(font);
		const int textW = fm.horizontalAdvance(label);
		const int textH = fm.ascent() + fm.descent();
		const int boxW = textW + padX * 2;
		const int boxH = textH + padY * 2;
		const double pillCenterX = center.x();
		const int boxX = static_cast<int>(pillCenterX - boxW / 2.0);
		const int boxY = 0;

		QColor textColor = m_labelTextColor.isValid() ? m_labelTextColor
							      : QApplication::palette().color(QPalette::Text);
		QColor bgColor = m_labelBgColor.isValid() ? m_labelBgColor
							  : QApplication::palette().color(QPalette::Base);
		if (!textColor.isValid() || textColor.lightness() < 40)
			textColor = QColor(235, 235, 235);
		if (!bgColor.isValid())
			bgColor = QColor(40, 40, 40);

		QRectF box(boxX, boxY, boxW, boxH);
		QColor pillBg = bgColor;
		pillBg.setAlpha(200);
		painter.setPen(Qt::NoPen);
		painter.setBrush(QBrush(pillBg));
		painter.drawRoundedRect(box, 3.0, 3.0);

		painter.setPen(textColor);
		painter.drawText(box, Qt::AlignCenter, label);
	}
}
