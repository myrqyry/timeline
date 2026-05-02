#include "timeline-serialization.h"
#include <QFile>

QJsonObject TimelineSerialization::toJsonObject(const TimelineModel &model, bool loop, double endTimeSeconds,
							bool snap, double snapIntervalMs, double bpm,
							bool gridToBpm, bool centerControls)
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
	settings["centerControls"] = centerControls;
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
	outDocument.centerControls = settings["centerControls"].toBool(false);
	return true;
}

bool TimelineSerialization::saveToFile(const QString &filePath, const QJsonObject &root, QString *errorMessage)
{
	QFile file(filePath);
	if (!file.open(QIODevice::WriteOnly)) {
		if (errorMessage) {
			*errorMessage = "Failed to open file for writing";
		}
		return false;
	}

	const QJsonDocument doc(root);
	if (file.write(doc.toJson()) < 0) {
		if (errorMessage) {
			*errorMessage = "Failed to write timeline JSON";
		}
		file.close();
		return false;
	}

	file.close();
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
