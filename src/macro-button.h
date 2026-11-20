#pragma once

#include <QString>
#include <QColor>
#include <QUuid>
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
	MacroParameterBinding bindingY; // XY controls only.
	QString bottomLabel;
	bool showValue = false;
	int xySize = 40;
	bool supportsLiveCaption() const { return kind != MacroControlKind::Action && kind != MacroControlKind::Text; }
	bool showsLiveCaption() const { return showValue && supportsLiveCaption(); }
	bool hasBottomCaption() const { return showsLiveCaption() || !bottomLabel.isEmpty(); }
	int padSize() const { return xySize == 48 || xySize == 64 ? xySize : 40; }
};
