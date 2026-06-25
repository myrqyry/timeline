#pragma once

#include <QPushButton>
#include <QString>
#include "macro-button.h"

class MacroButtonWidget : public QPushButton {
	Q_OBJECT
public:
	explicit MacroButtonWidget(const MacroButton &button, QWidget *parent = nullptr);

	void setButton(const MacroButton &button);
	MacroButton button() const { return m_button; }

	void refreshIcon(const QString &colorName);
	void setPressedAppearance(bool pressed);

signals:
	void fireRequested(const MacroButton &button);

private:
	MacroButton m_button;
};