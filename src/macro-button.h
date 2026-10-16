#pragma once

#include <QString>
#include <QColor>
#include <QUuid>
#include "event-types.h"

struct MacroButton {
	QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
	QString label;
	TimelineEventType type = TimelineEventType::SceneSwitch;
	QString sourceName;
	QString filterName;
	QString data; // Serialized string; ObsExecutor reads via QVariant conversion when present
	QColor color = QColor(0, 200, 255);
};
