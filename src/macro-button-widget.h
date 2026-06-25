#pragma once

#include <QPushButton>
#include <QString>
#include "macro-button.h"

class MacroButtonWidget : public QPushButton {
	Q_OBJECT
public:
	explicit MacroButtonWidget(const MacroButton &button, int sidePx, QWidget *parent = nullptr);

	void setButton(const MacroButton &button);
	void setSidePx(int sidePx);
	MacroButton button() const { return m_button; }

	void refreshIcon(const QString &colorName);
	void setPressedAppearance(bool pressed);

	// Match the timeline toolbar button sizes (Add Event, Snap, Loop, etc.).
	// 28 px normally, 24 px when the timeline dock is in compact mode.
	static int stripSidePxForCompact(bool compact);
	static int stripHeightForCompact(bool compact);

signals:
	void fireRequested(const MacroButton &button);

private:
	MacroButton m_button;
	int m_sidePx = stripSidePxForCompact(false);
};