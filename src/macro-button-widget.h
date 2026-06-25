#pragma once

#include <QWidget>
#include <QColor>
#include <QString>
#include <QSvgRenderer>
#include "macro-button.h"

class TestTimelineMacroButtonAccessor;

// MacroButtonWidget renders a circular event bubble that matches the timeline's
// compact event style (22 px circle + label pill + theme-aware icon). It is a
// custom QWidget (not QPushButton) so paint() can mirror TimelineEventItem's
// bubble drawing. Clicking emits fireRequested.
class MacroButtonWidget : public QWidget {
	Q_OBJECT
	// Test class is allowed to invoke the protected mouse handlers directly.
	friend class TestTimelineMacroButtonAccessor;
public:
	explicit MacroButtonWidget(const MacroButton &button, QWidget *parent = nullptr);

	void setButton(const MacroButton &button);
	MacroButton button() const { return m_button; }

	void refreshIcon(const QString &colorName);
	void setPressedAppearance(bool pressed);

	void setBorderColor(const QColor &color) { m_borderColor = color; update(); }
	void setSelectionColor(const QColor &color) { m_selectionColor = color; update(); }
	void setBackgroundColor(const QColor &color) { m_backgroundColor = color; update(); }
	void setLabelColors(const QColor &textColor, const QColor &bgColor)
	{
		m_labelTextColor = textColor;
		m_labelBgColor = bgColor;
		update();
	}

	// Match the timeline's compact event bubble (22 px).
	static constexpr double kBubbleSize = 22.0;

signals:
	void fireRequested(const MacroButton &button);

protected:
	void mousePressEvent(QMouseEvent *event) override;
	void mouseReleaseEvent(QMouseEvent *event) override;
	void paintEvent(QPaintEvent *event) override;
	void enterEvent(QEnterEvent *event) override;
	void leaveEvent(QEvent *event) override;

public:
	QSize sizeHint() const override;
	QSize minimumSizeHint() const override;

private:
	void rebuildRenderer();

	MacroButton m_button;
	QSvgRenderer *m_renderer = nullptr;

	bool m_isHovered = false;
	bool m_isPressed = false;

	QColor m_borderColor = Qt::black;
	QColor m_selectionColor = QColor(255, 200, 0);
	QColor m_backgroundColor = Qt::white;
	QColor m_labelTextColor;
	QColor m_labelBgColor;
};