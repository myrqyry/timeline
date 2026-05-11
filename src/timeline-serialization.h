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
	double snapIntervalMs = 500.0;
	double bpm = 120.0;
	bool gridToBpm = false;
	double noteLengthBeats = 1.0; // Quarter note = 1 beat
	bool centerControls = false;
	bool alternatingBg = false;
	double currentTimeMs = 0.0;
	double zoomPixelsPerSecond = 100.0;
};

class TimelineSerialization {
public:
	static QJsonObject toJsonObject(const TimelineModel &model, bool loop, double endTimeSeconds, bool snap,
					double snapIntervalMs, double bpm, bool gridToBpm, double noteLengthBeats,
					bool centerControls, bool alternatingBg, double currentTimeMs = 0.0,
					double zoomPixelsPerSecond = 100.0);

	static bool fromJsonDocument(const QJsonDocument &doc, TimelineModel &model, TimelineDocument &outDocument,
				     QString *errorMessage = nullptr);

	static bool saveToFile(const QString &filePath, const QJsonObject &root, QString *errorMessage = nullptr);

	static bool loadFromFile(const QString &filePath, QJsonDocument &outDoc, QString *errorMessage = nullptr);
};
