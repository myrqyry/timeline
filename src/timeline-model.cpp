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
