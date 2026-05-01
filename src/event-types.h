#pragma once

#include <QString>
#include <QVariant>
#include <QColor>

enum class TimelineEventType {
    SceneSwitch,
    MediaStart,
    MediaStop,
    MediaSeek, // TODO: Planned future feature (seek to position in media source)
    SourceShow,
    SourceHide,
    FilterEnable,
    FilterDisable,
    HotkeyTrigger,
    TriggerTimeline
};

struct TimelineEvent {
    double timeMs;
    TimelineEventType type;
    QString sourceName; // Source name, scene name, or hotkey name
    QString filterName; // Optional: Filter name if targeting a filter
    QVariant data;    // Additional data (e.g., volume level, seek position)
    QColor color = QColor(0, 200, 255); // Default cyan color
    QString customIconPath; // Path to custom SVG icon
    
    // Duration
    bool hasDuration = false;
    double durationMs = 0.0;
};

