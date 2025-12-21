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

QJsonArray TimelineModel::toJson() const {
    QJsonArray array;
    for (const auto& evt : events) {
        QJsonObject obj;
        obj["time"] = evt.timeMs;
        obj["type"] = static_cast<int>(evt.type);
        obj["target"] = evt.targetId;
        // Skipping data variant for now for simplicity, or convert to string
        array.append(obj);
    }
    return array;
}

void TimelineModel::fromJson(const QJsonArray &json) {
    clearEvents();
    for (const auto& val : json) {
        QJsonObject obj = val.toObject();
        TimelineEvent evt;
        evt.timeMs = obj["time"].toDouble();
        evt.type = static_cast<TimelineEventType>(obj["type"].toInt());
        evt.targetId = obj["target"].toString();
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
