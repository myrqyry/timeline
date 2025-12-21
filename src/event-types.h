#pragma once

#include <QString>
#include <QVariant>
#include <QColor>

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
    QColor color = QColor(0, 200, 255); // Default cyan color
};

