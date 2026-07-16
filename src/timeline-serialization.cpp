#include "timeline-serialization.h"
#include <QFile>
#include <QSaveFile>

QJsonObject TimelineSerialization::toJsonObject(const TimelineModel &model, bool loop, double endTimeSeconds, bool snap,
						double snapIntervalMs, double bpm, bool gridToBpm,
						double noteLengthBeats, bool centerControls, bool alternatingBg,
						bool compactMode, bool showEndTimeOnDock, double currentTimeMs,
						double zoomPixelsPerSecond)
{
	QJsonObject root;
	root["events"] = model.toJson();

	QJsonObject settings;
	settings["loop"] = loop;
	settings["endTime"] = endTimeSeconds;
	settings["snap"] = snap;
	settings["snapIntervalMs"] = snapIntervalMs;
	settings["bpm"] = bpm;
	settings["gridToBpm"] = gridToBpm;
	settings["noteLengthBeats"] = noteLengthBeats;
	settings["centerControls"] = centerControls;
	settings["alternatingBg"] = alternatingBg;
	settings["compactMode"] = compactMode;
	settings["showEndTimeOnDock"] = showEndTimeOnDock;
	settings["currentTimeMs"] = currentTimeMs;
	settings["zoomPixelsPerSecond"] = zoomPixelsPerSecond;
	root["settings"] = settings;
	return root;
}

bool TimelineSerialization::fromJsonDocument(const QJsonDocument &doc, TimelineModel &model,
					     TimelineDocument &outDocument, QString *errorMessage)
{
	if (doc.isArray()) {
		// Legacy format
		model.fromJson(doc.array());
		outDocument.events = doc.array();
		return true;
	}

	if (!doc.isObject()) {
		if (errorMessage) {
			*errorMessage = "Timeline JSON must be an array or object";
		}
		return false;
	}

	const QJsonObject root = doc.object();
	const QJsonArray events = root["events"].toArray();
	model.fromJson(events);

	outDocument.events = events;
	const QJsonObject settings = root["settings"].toObject();
	outDocument.loop = settings["loop"].toBool(false);
	outDocument.endTimeSeconds = settings["endTime"].toDouble(0.0);
	outDocument.snap = settings["snap"].toBool(false);
	outDocument.snapIntervalMs = settings["snapIntervalMs"].toDouble(500.0);
	outDocument.bpm = settings["bpm"].toDouble(120.0);
	outDocument.gridToBpm = settings["gridToBpm"].toBool(false);
	outDocument.noteLengthBeats = settings["noteLengthBeats"].toDouble(1.0);
	outDocument.centerControls = settings["centerControls"].toBool(false);
	outDocument.alternatingBg = settings["alternatingBg"].toBool(false);
	outDocument.compactMode = settings["compactMode"].toBool(false);
	outDocument.showEndTimeOnDock = settings["showEndTimeOnDock"].toBool(true);
	outDocument.currentTimeMs = settings["currentTimeMs"].toDouble(0.0);
	outDocument.zoomPixelsPerSecond = settings["zoomPixelsPerSecond"].toDouble(100.0);
	return true;
}

bool TimelineSerialization::saveToFile(const QString &filePath, const QJsonObject &root, QString *errorMessage)
{
	QSaveFile file(filePath);
	if (!file.open(QIODevice::WriteOnly)) {
		if (errorMessage) {
			*errorMessage = "Failed to open file for writing: " + file.errorString();
		}
		return false;
	}

	const QJsonDocument doc(root);
	const QByteArray json = doc.toJson();
	if (file.write(json) != json.size()) {
		if (errorMessage) {
			*errorMessage = "Failed to write timeline JSON";
		}
		return false;
	}

	if (!file.commit()) {
		if (errorMessage) {
			*errorMessage = "Failed to commit and finalize save: " + file.errorString();
		}
		return false;
	}

	return true;
}

bool TimelineSerialization::loadFromFile(const QString &filePath, QJsonDocument &outDoc, QString *errorMessage)
{
	QFile file(filePath);
	if (!file.exists() || !file.open(QIODevice::ReadOnly)) {
		if (errorMessage) {
			*errorMessage = "Failed to open timeline file";
		}
		return false;
	}

	QJsonParseError parseError;
	outDoc = QJsonDocument::fromJson(file.readAll(), &parseError);
	file.close();
	if (parseError.error != QJsonParseError::NoError) {
		if (errorMessage) {
			*errorMessage = parseError.errorString();
		}
		return false;
	}

	return true;
}
