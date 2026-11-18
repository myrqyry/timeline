#include "timeline-model.h"
#include "theme-helpers.h"
#include <QJsonObject>
#include <QSet>
#include <obs-module.h>

TimelineModel::TimelineModel(QObject *parent) : QObject(parent)
{
	blog(LOG_INFO, "[Timeline] ├─ Model     : created");
}

namespace {
bool sameEvent(const TimelineEvent &a, const TimelineEvent &b)
{
	return a.id == b.id && a.timeMs == b.timeMs && a.type == b.type && a.sourceName == b.sourceName &&
	       a.filterName == b.filterName && a.data == b.data && a.color == b.color &&
	       a.customIconPath == b.customIconPath && a.label == b.label && a.showLabel == b.showLabel &&
	       a.hasDuration == b.hasDuration && a.durationMs == b.durationMs;
}
} // namespace

void TimelineModel::setHistoryEnabled(bool enabled)
{
	if (historyEnabled == enabled)
		return;
	historyEnabled = enabled;
	resetHistory();
}

void TimelineModel::resetHistory()
{
	undoHistory.clear();
	redoHistory.clear();
	emit historyChanged();
}

bool TimelineModel::commitEdit(EventList next)
{
	std::stable_sort(next.begin(), next.end(),
			 [](const TimelineEvent &a, const TimelineEvent &b) { return a.timeMs < b.timeMs; });
	if (next.size() == events.size() && std::equal(next.begin(), next.end(), events.begin(), sameEvent))
		return false;
	if (historyEnabled) {
		undoHistory.push_back(std::move(events));
		if (undoHistory.size() > HistoryLimit)
			undoHistory.pop_front();
		redoHistory.clear();
	}
	events = std::move(next);
	emit modelChanged();
	emit historyChanged();
	return true;
}

bool TimelineModel::undo()
{
	if (!canUndo())
		return false;
	redoHistory.push_back(std::move(events));
	events = std::move(undoHistory.back());
	undoHistory.pop_back();
	emit modelChanged();
	emit historyChanged();
	return true;
}

bool TimelineModel::redo()
{
	if (!canRedo())
		return false;
	undoHistory.push_back(std::move(events));
	events = std::move(redoHistory.back());
	redoHistory.pop_back();
	emit modelChanged();
	emit historyChanged();
	return true;
}

void TimelineModel::addEvent(const TimelineEvent &event)
{
	addEvents({event});
}

void TimelineModel::addEvents(const EventList &batch)
{
	if (batch.empty())
		return;
	auto next = events;
	next.insert(next.end(), batch.begin(), batch.end());
	commitEdit(std::move(next));
}

void TimelineModel::removeEventsById(const QStringList &ids)
{
	if (ids.isEmpty())
		return;
	const QSet<QString> selected(ids.begin(), ids.end());
	auto next = events;
	next.erase(std::remove_if(next.begin(), next.end(),
				  [&](const TimelineEvent &evt) { return selected.contains(evt.id); }),
		   next.end());
	commitEdit(std::move(next));
}

void TimelineModel::setEventTimes(const QHash<QString, double> &times)
{
	if (times.isEmpty())
		return;
	auto next = events;
	for (auto &evt : next) {
		const auto it = times.constFind(evt.id);
		if (it != times.cend())
			evt.timeMs = it.value();
	}
	commitEdit(std::move(next));
}

const std::vector<TimelineEvent> &TimelineModel::getEvents() const
{
	return events;
}

void TimelineModel::clearEvents()
{
	commitEdit({});
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
	removeEventsById({id});
	return true;
}

bool TimelineModel::updateEventTimeById(const QString &id, double newTimeMs)
{
	const int index = findEventIndexById(id);
	if (index < 0) {
		return false;
	}
	setEventTimes({{id, newTimeMs}});
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
	auto next = events;
	next[index] = updated;
	commitEdit(std::move(next));
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
		constexpr int maxTypeValue = static_cast<int>(TimelineEventType::SetParameterValue);
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

		if (evt.type != TimelineEventType::SetParameterValue && obj.contains("hasDuration")) {
			evt.hasDuration = obj["hasDuration"].toBool();
			evt.durationMs = obj["duration"].toDouble();
		}
		if (obj.contains("data")) {
			evt.data = obj["data"].toVariant();
		}

		events.push_back(evt);
	}
	sortEvents();
	resetHistory();
	emit modelChanged();
}

void TimelineModel::sortEvents()
{
	std::stable_sort(events.begin(), events.end(),
			 [](const TimelineEvent &a, const TimelineEvent &b) { return a.timeMs < b.timeMs; });
}
