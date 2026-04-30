#include "timeline-model.h"
#include <obs-module.h>

TimelineModel::TimelineModel(QObject *parent) : QObject(parent) {
    blog(LOG_INFO, "TimelineModel created");
}

void TimelineModel::addEvent(const TimelineEvent &event) {
    events.push_back(event);
    sortEvents();
    emit modelChanged();
}

void TimelineModel::removeEvent(int index) {
    if (index >= 0 && index < events.size()) {
        events.erase(events.begin() + index);
        emit modelChanged();
    }
}

const std::vector<TimelineEvent>& TimelineModel::getEvents() const {
    return events;
}

void TimelineModel::clearEvents() {
    events.clear();
    emit modelChanged();
}

void TimelineModel::updateEventTime(int index, double newTimeMs) {
    if (index >= 0 && index < events.size()) {
        events[index].timeMs = newTimeMs;
        sortEvents();
        emit modelChanged();
    }
}

void TimelineModel::replaceEvent(int index, const TimelineEvent &event) {
    if (index >= 0 && index < events.size()) {
        events[index] = event;
        sortEvents();
        emit modelChanged();
    }
}

int TimelineModel::findEventIndex(double timeMs) const {
    for (size_t i = 0; i < events.size(); i++) {
        if (std::abs(events[i].timeMs - timeMs) < 10.0) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

QJsonArray TimelineModel::toJson() const {
    QJsonArray array;
    for (const auto& evt : events) {
        QJsonObject obj;
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
        
        if (evt.hasDuration) {
            obj["hasDuration"] = true;
            obj["duration"] = evt.durationMs;
        }
        if (!evt.data.isNull()) {
            obj["data"] = evt.data.toString();
        }
        array.append(obj);
    }
    return array;
}

#include "theme-helpers.h"

void TimelineModel::fromJson(const QJsonArray &json) {
    clearEvents();
    for (const auto& val : json) {
        QJsonObject obj = val.toObject();
        TimelineEvent evt;
        evt.timeMs = obj["time"].toDouble();
        evt.type = static_cast<TimelineEventType>(obj["type"].toInt());
        if (obj.contains("source")) {
            evt.sourceName = obj["source"].toString();
            if (obj.contains("filter")) {
                evt.filterName = obj["filter"].toString();
            }
        } else if (obj.contains("target")) {
            // Legacy migration
            QString target = obj["target"].toString();
            // If it looks like Source::Filter and needs splitting based on type? 
            // Better safe strategy: if it contains :: split it
            if (target.contains("::") && (evt.type == TimelineEventType::FilterEnable || evt.type == TimelineEventType::FilterDisable)) {
                 QStringList parts = target.split("::");
                 if (parts.size() >= 2) {
                     evt.sourceName = parts[0];
                     evt.filterName = parts[1];
                 } else {
                     evt.sourceName = target;
                 }
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
        
        if (obj.contains("hasDuration")) {
            evt.hasDuration = obj["hasDuration"].toBool();
            evt.durationMs = obj["duration"].toDouble();
        }
        if (obj.contains("data")) {
            evt.data = obj["data"].toString();
        }

        events.push_back(evt);
    }
    sortEvents();
    emit modelChanged();
}

void TimelineModel::sortEvents() {
    std::sort(events.begin(), events.end(), [](const TimelineEvent &a, const TimelineEvent &b) {
        return a.timeMs < b.timeMs;
    });
}
