#pragma once

#include <QObject>
#include <vector>
#include <algorithm>
#include <optional>
#include <deque>
#include <QHash>
#include <QStringList>
#include "event-types.h"

#include <QJsonArray>


class TimelineModel : public QObject {
    Q_OBJECT

public:
    explicit TimelineModel(QObject *parent = nullptr);

    void addEvent(const TimelineEvent &event);
    void addEvents(const std::vector<TimelineEvent> &batch);
    void removeEventsById(const QStringList &ids);
    void setEventTimes(const QHash<QString, double> &times);

    // Session-only edit history; disabled for headless playback models.
    void setHistoryEnabled(bool enabled);
    bool canUndo() const { return !undoHistory.empty(); }
    bool canRedo() const { return !redoHistory.empty(); }
    bool undo();
    bool redo();
    const std::vector<TimelineEvent>& getEvents() const;
    void clearEvents();

    // Updates
    int findEventIndexById(const QString &id) const;
    bool removeEventById(const QString &id);
    bool updateEventTimeById(const QString &id, double newTimeMs);
    bool replaceEventById(const QString &id, const TimelineEvent &event);
    std::optional<TimelineEvent> findEventById(const QString &id) const;

    // Serialization
    QJsonArray toJson() const;
    void fromJson(const QJsonArray &json);
    
    // Sort events by time
    void sortEvents();

signals:
    void modelChanged();
    void historyChanged();

private:
    using EventList = std::vector<TimelineEvent>;
    bool commitEdit(EventList next);
    void resetHistory();
    EventList events;
    bool historyEnabled = false;
    static constexpr size_t HistoryLimit = 100;
    std::deque<EventList> undoHistory;
    std::deque<EventList> redoHistory;
};
