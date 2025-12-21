#pragma once

#include <QObject>
#include <vector>
#include <algorithm>
#include "event-types.h"

#include <QVariant>
#include <QJsonArray>
#include <QJsonObject>

class TimelineModel : public QObject {
    Q_OBJECT

public:
    explicit TimelineModel(QObject *parent = nullptr);

    void addEvent(const TimelineEvent &event);
    void removeEvent(int index);
    const std::vector<TimelineEvent>& getEvents() const;
    void clearEvents();

    // Serialization
    QJsonArray toJson() const;
    void fromJson(const QJsonArray &json);
    
    // Sort events by time
    void sortEvents();

signals:
    void modelChanged();

private:
    std::vector<TimelineEvent> events;
};
