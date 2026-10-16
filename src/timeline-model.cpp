#include "timeline-model.h"
#include "theme-helpers.h"
#include <QJsonObject>
#include <obs-module.h>

TimelineModel::TimelineModel(QObject *parent) : QObject(parent)
{
	blog(LOG_INFO, "[Timeline] ├─ Model     : created");
}

void TimelineModel::addEvent(const TimelineEvent &event)
{
	events.push_back(event);
	sortEvents();
	emit modelChanged();
}

const std::vector<TimelineEvent> &TimelineModel::getEvents() const
{
	return events;
}

void TimelineModel::clearEvents()
{
	events.clear();
	emit modelChanged();
}

int TimelineModel::findEventIndexById(const QString &id) const
{
	for (size_t i = 0; i < events.size(); ++i) {
		if (events[i].id == id) {
			return static_cast<int>(i);
		}
	}
	return -1;
}

bool TimelineModel::removeEventById(const QString &id)
{
	const int index = findEventIndexById(id);
	if (index < 0) {
		return false;
	}
	events.erase(events.begin() + index);
	emit modelChanged();
	return true;
}

bool TimelineModel::updateEventTimeById(const QString &id, double newTimeMs)
{
	const int index = findEventIndexById(id);
	if (index < 0) {
		return false;
	}
	events[index].timeMs = newTimeMs;
	sortEvents();
	emit modelChanged();
	return true;
}

bool TimelineModel::replaceEventById(const QString &id, const TimelineEvent &event)
{
	const int index = findEventIndexById(id);
	if (index < 0) {
		return false;
	}
	TimelineEvent updated = event;
	updated.id = id;
	events[index] = updated;
	sortEvents();
	emit modelChanged();
	return true;
}

std::optional<TimelineEvent> TimelineModel::findEventById(const QString &id) const
{
	const int index = findEventIndexById(id);
	if (index < 0) {
		return std::nullopt;
	}
	return events[static_cast<size_t>(index)];
}

QJsonArray TimelineModel::toJson() const
{
	QJsonArray array;
	for (const auto &evt : events) {
		QJsonObject obj;
		obj["id"] = evt.id;
		obj["time"] = evt.timeMs;
		obj["type"] = static_cast<int>(evt.type);
		obj["source"] = evt.sourceName;
		if (!evt.filterName.isEmpty()) {
			obj["filter"] = evt.filterName;
		}
		obj["color"] = evt.color.name();
		if (!evt.customIconPath.isEmpty()) {
			obj["customIconPath"] = evt.customIconPath;
		}

		if (!evt.label.isEmpty()) {
			obj["label"] = evt.label;
		}
		if (evt.showLabel) {
			obj["showLabel"] = true;
		}

		if (evt.hasDuration) {
			obj["hasDuration"] = true;
			obj["duration"] = evt.durationMs;
		}
		if (!evt.data.isNull()) {
			obj["data"] = QJsonValue::fromVariant(evt.data);
		}
		array.append(obj);
	}
	return array;
}

void TimelineModel::fromJson(const QJsonArray &json)
{
	events.clear();
	for (const auto val : json) {
		QJsonObject obj = val.toObject();
		TimelineEvent evt;
		evt.id = obj["id"].toString();
		if (evt.id.isEmpty()) {
			evt.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
		}
		evt.timeMs = obj["time"].toDouble();
		int typeInt = obj["type"].toInt(-1);
		constexpr int maxTypeValue = static_cast<int>(TimelineEventType::TriggerTimeline);
		if (typeInt < 0 || typeInt > maxTypeValue) {
			blog(LOG_WARNING, "[Timeline] ┌─ Unknown event type skipped");
			blog(LOG_WARNING, "[Timeline] └─ Type : %d", typeInt);
			continue;
		}
		evt.type = static_cast<TimelineEventType>(typeInt);
		if (obj.contains("source")) {
			evt.sourceName = obj["source"].toString();
			if (obj.contains("filter")) {
				evt.filterName = obj["filter"].toString();
			}
		} else if (obj.contains("target")) {
			// Legacy migration: older files used "target" instead of "source"/"filter".
			// Filter events stored their value as "Source::FilterName".
			QString target = obj["target"].toString();
			if (target.contains("::") && (evt.type == TimelineEventType::FilterEnable ||
						      evt.type == TimelineEventType::FilterDisable)) {
				QStringList parts = target.split("::");
				evt.sourceName = parts[0];
				evt.filterName = parts[1];
			} else {
				evt.sourceName = target;
			}
		}

		if (obj.contains("color")) {
			evt.color = QColor(obj["color"].toString());
		} else {
			// Use default theme color for this type if missing (legacy files)
			evt.color = ThemeIcons::getDefaultColor(evt.type);
		}

		if (obj.contains("customIconPath")) {
			evt.customIconPath = obj["customIconPath"].toString();
		}

		if (obj.contains("label")) {
			evt.label = obj["label"].toString();
		}
		evt.showLabel = obj["showLabel"].toBool(false);

		if (obj.contains("hasDuration")) {
			evt.hasDuration = obj["hasDuration"].toBool();
			evt.durationMs = obj["duration"].toDouble();
		}
		if (obj.contains("data")) {
			evt.data = obj["data"].toVariant();
		}

		events.push_back(evt);
	}
	sortEvents();
	emit modelChanged();
}

void TimelineModel::sortEvents()
{
	std::sort(events.begin(), events.end(),
		  [](const TimelineEvent &a, const TimelineEvent &b) { return a.timeMs < b.timeMs; });
}
