#include "macro-button-widget.h"
#include "theme-helpers.h"

#include <QImage>

static QString fallbackLabel(const MacroButton &b)
{
	if (!b.label.isEmpty())
		return b.label;
	return b.sourceName.isEmpty() ? ThemeIcons::typeDisplayName(b.type) : b.sourceName;
}

MacroButtonWidget::MacroButtonWidget(const MacroButton &button, QWidget *parent)
	: QPushButton(parent), m_button(button)
{
	constexpr int side = kStripButtonSize;
	setMinimumSize(side, side);
	setMaximumSize(side, side);
	setIconSize(QSize(20, 20));
	setText(fallbackLabel(m_button));
	setToolTip(fallbackLabel(m_button));
	refreshIcon(ThemeIcons::iconColorFor(this).name());
	connect(this, &QPushButton::clicked, this, [this]() { emit fireRequested(m_button); });
}

void MacroButtonWidget::setButton(const MacroButton &button)
{
	m_button = button;
	setText(fallbackLabel(m_button));
	setToolTip(fallbackLabel(m_button));
	refreshIcon(ThemeIcons::iconColorFor(this).name());
}

void MacroButtonWidget::refreshIcon(const QString &colorName)
{
	const QByteArray svg = ThemeIcons::getIconSvg(m_button.type, colorName);
	const QPixmap pix = QPixmap::fromImage(QImage::fromData(svg));
	if (!pix.isNull()) {
		setIcon(QIcon(pix));
	}
}

void MacroButtonWidget::setPressedAppearance(bool pressed)
{
	if (pressed) {
		setDown(true);
		setStyleSheet(QString("QPushButton { background-color: %1; }").arg(m_button.color.name()));
	} else {
		setDown(false);
		setStyleSheet(QString());
	}
}