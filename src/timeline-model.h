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
    const std::vector<TimelineEvent>& getEvents() const;
    void clearEvents();

    // Updates
    int findEventIndexById(const QString &id) const;
    bool removeEventById(const QString &id);
    bool updateEventTimeById(const QString &id, double newTimeMs);
    bool replaceEventById(const QString &id, const TimelineEvent &event);
    const TimelineEvent *findEventById(const QString &id) const;

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
