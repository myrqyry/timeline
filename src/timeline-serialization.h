#pragma once

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>
#include "timeline-model.h"

struct TimelineDocument {
	QJsonArray events;
	bool loop = false;
	double endTimeSeconds = 0.0;
	bool snap = false;
	double bpm = 120.0;
	bool gridToBpm = false;
	bool centerControls = false;
};

class TimelineSerialization {
public:
	static QJsonObject toJsonObject(const TimelineModel &model, bool loop, double endTimeSeconds, bool snap,
					double bpm, bool gridToBpm, bool centerControls);

	static bool fromJsonDocument(const QJsonDocument &doc, TimelineModel &model, TimelineDocument &outDocument,
				     QString *errorMessage = nullptr);

	static bool saveToFile(const QString &filePath, const QJsonObject &root, QString *errorMessage = nullptr);

	static bool loadFromFile(const QString &filePath, QJsonDocument &outDoc, QString *errorMessage = nullptr);
};
