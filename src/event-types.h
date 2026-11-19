#pragma once

#include <QString>
#include <QVariant>
#include <QColor>
#include <QUuid>
#include <cmath>

enum class TimelineEventType {
	SceneSwitch,
	MediaStart,
	MediaStop,
	MediaSeek, // Deprecated — reserved, not exposed in UI
	SourceShow,
	SourceHide,
	FilterEnable,
	FilterDisable,
	HotkeyTrigger,
	TriggerTimeline,
	DownstreamKeyerScene,
	MediaPause,
	MediaResume,
	MediaSeekTo,
	SetParameterValue,
	FreezeTime // Append only: persisted enum IDs must not shift
};

struct TimelineEvent {
	QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
	double timeMs = 0.0;
	TimelineEventType type = TimelineEventType::SceneSwitch;
	QString sourceName;                 // Source name, scene name, or hotkey name
	QString filterName;                 // Optional: Filter name if targeting a filter
	QVariant data;                      // Additional data (e.g., volume level, seek position)
	QColor color = QColor(0, 200, 255); // Default cyan color
	QString customIconPath;             // Path to custom SVG icon
	QString label;                      // Optional display label for the event
	bool showLabel = false;             // Whether to display the label on the timeline

	// Duration
	bool hasDuration = false;
	double durationMs = 0.0;
};

inline bool isMediaAction(TimelineEventType type)
{
	return type == TimelineEventType::MediaStart || type == TimelineEventType::MediaStop ||
	       type == TimelineEventType::MediaPause || type == TimelineEventType::MediaResume ||
	       type == TimelineEventType::MediaSeekTo;
}

// A freeze is a point cue with a wall-clock wait, not a timeline duration.
inline constexpr double kMaxFreezeMs = 86400000.0;
inline double freezeDurationMs(const TimelineEvent &evt)
{
	if (evt.type != TimelineEventType::FreezeTime)
		return 0.0;
	bool ok = false;
	const double ms = evt.data.toDouble(&ok);
	return ok && std::isfinite(ms) && ms > 0.0 && ms <= kMaxFreezeMs ? ms : 0.0;
}
