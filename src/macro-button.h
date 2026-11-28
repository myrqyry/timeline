#pragma once

#include <QString>
#include <QColor>
#include <QUuid>
#include <vector>

enum class MacroOrientation { FollowStrip, Horizontal, Vertical };
#include "event-types.h"
#include "macro-parameter.h"

struct MacroButton {
	QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
	QString label;
	TimelineEventType type = TimelineEventType::SceneSwitch;
	QString sourceName;
	QString filterName;
	QString data; // Serialized string; ObsExecutor reads via QVariant conversion when present
	QColor color = QColor(0, 200, 255);
	MacroControlKind kind = MacroControlKind::Action;
	MacroParameterBinding binding;
	MacroParameterBinding bindingY;                           // XY controls only.
	std::optional<MacroParameter::ToggleValues> toggleValues; // Non-boolean toggle presets.
	QString bottomLabel;
	bool showValue = false;
	int xySize = 40;
	static constexpr int MaxGap = 2048;
	int gapBefore = 0;
	MacroOrientation orientation = MacroOrientation::FollowStrip;
	// A grid owns exactly four action records; an empty ID denotes an unassigned cell.
	std::vector<MacroButton> gridActions;
	bool verticalSlider(bool stripVertical = false) const
	{
		return kind == MacroControlKind::Slider &&
		       (orientation == MacroOrientation::Vertical ||
			(orientation == MacroOrientation::FollowStrip && stripVertical));
	}
	bool supportsLiveCaption() const
	{
		return kind != MacroControlKind::Action && kind != MacroControlKind::Text &&
		       kind != MacroControlKind::ButtonGrid;
	}
	bool showsLiveCaption() const { return showValue && supportsLiveCaption(); }
	bool hasBottomCaption() const { return showsLiveCaption() || !bottomLabel.isEmpty(); }
	int padSize() const { return xySize == 48 || xySize == 64 ? xySize : 40; }
};
