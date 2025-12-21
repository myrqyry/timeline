#pragma once

#include <QString>
#include <QVariant>

enum class TimelineEventType {
    SceneSwitch,
    MediaStart,
    MediaStop,
    MediaSeek,
    SourceShow,
    SourceHide,
    FilterEnable,
    FilterDisable
};

struct TimelineEvent {
    double timeMs;
    TimelineEventType type;
    QString targetId; // Scene name, Source name, etc.
    QVariant data;    // Additional data (e.g., volume level, seek position)
};
