#pragma once
#include <QColor>
#include <QString>
#include <QByteArray>
#include "event-types.h"

// Material Design Icons paths
namespace ThemeIcons {

// SVG Wrapper template
// Note: The color is applied via fill on the root <svg> element. Custom SVGs
// with inline fill/style attributes on inner elements will not adapt to the
// event background contrast. Use SVGs without inline fills for best results.
inline QByteArray wrapSvg(const QString& path, const QString& color) {
    return QString("<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 24 24\" width=\"24\" height=\"24\" fill=\"%2\">%1</svg>")
            .arg(path, color).toUtf8();
}

inline const char* PATH_SCENE_SWITCH = "<path d=\"M4 6H2v14c0 1.1.9 2 2 2h14v-2H4V6zm16-4H8c-1.1 0-2 .9-2 2v12c0 1.1.9 2 2 2h12c1.1 0 2-.9 2-2V4c0-1.1-.9-2-2-2zm0 14H8V4h12v12z\"/>";
inline const char* PATH_SOURCE_SHOW = "<path d=\"M12 4.5C7 4.5 2.73 7.61 1 12c1.73 4.39 6 7.5 11 7.5s9.27-3.11 11-7.5c-1.73-4.39-6-7.5-11-7.5zM12 17c-2.76 0-5-2.24-5-5s2.24-5 5-5 5 2.24 5 5-2.24 5-5 5zm0-8c-1.66 0-3 1.34-3 3s1.34 3 3 3 3-1.34 3-3-1.34-3-3-3z\"/>";
inline const char* PATH_SOURCE_HIDE = "<path d=\"M12 7c2.76 0 5 2.24 5 5 0 .65-.13 1.26-.36 1.83l2.92 2.92c1.51-1.26 2.7-2.89 3.43-4.75-1.73-4.39-6-7.5-11-7.5-1.4 0-2.74.25-3.98.7l2.16 2.16C10.74 7.13 11.35 7 12 7zM2 4.27l2.28 2.28.46.46C3.08 8.3 1.78 10.02 1 12c1.73 4.39 6 7.5 11 7.5 1.55 0 3.03-.3 4.38-.88l.42.42 5.42 5.42 1.27-1.27L3.27 3 2 4.27zM7.53 9.8l1.55 1.55c-.05.21-.08.43-.08.65 0 1.66 1.34 3 3 3 .22 0 .44-.03.65-.08l1.55 1.55c-.67.33-1.41.53-2.2.53-2.76 0-5-2.24-5-5 0-.79.2-1.53.53-2.2zm4.31-.78l3.15 3.15.02-.16c0-1.66-1.34-3-3-3l-.17.01z\"/>";
inline const char* PATH_MEDIA_START = "<path d=\"M8 5v14l11-7z\"/>";
inline const char* PATH_MEDIA_STOP = "<path d=\"M6 6h12v12H6z\"/>";
inline const char* PATH_FILTER_ENABLE = "<path d=\"M9 16.17L4.83 12l-1.42 1.41L9 19 21 7l-1.41-1.41z\"/>";
inline const char* PATH_FILTER_DISABLE = "<path d=\"M19 6.41L17.59 5 12 10.59 6.41 5 5 6.41 10.59 12 5 17.59 6.41 19 12 13.41 17.59 19 19 17.59 13.41 12z\"/>";
inline const char* PATH_HOTKEY = "<path d=\"M20 5H4c-1.1 0-1.99.9-1.99 2L2 17c0 1.1.9 2 2 2h16c1.1 0 2-.9 2-2V7c0-1.1-.9-2-2-2zm-9 3h2v2h-2V8zm0 3h2v2h-2v-2zM8 8h2v2H8V8zm0 3h2v2H8v-2zm-1 2H5v-2h2v2zm0-3H5V8h2v2zm9 7H8v-2h8v2zm0-4h-2v-2h2v2zm0-3h-2V8h2v2z\"/>";
inline const char* PATH_TRIGGER_TIMELINE = "<path d=\"M4 10h12v2H4zm0-4h12v2H4zm0 8h8v2H4zm10 0v6l5-3z\"/>";

inline QByteArray getIconSvg(TimelineEventType type, const QString& color = "white") {
    switch (type) {
        case TimelineEventType::SceneSwitch:   return wrapSvg(PATH_SCENE_SWITCH, color);
        case TimelineEventType::SourceShow:    return wrapSvg(PATH_SOURCE_SHOW, color);
        case TimelineEventType::SourceHide:    return wrapSvg(PATH_SOURCE_HIDE, color);
        case TimelineEventType::MediaStart:    return wrapSvg(PATH_MEDIA_START, color);
        case TimelineEventType::MediaStop:     return wrapSvg(PATH_MEDIA_STOP, color);
        case TimelineEventType::FilterEnable:  return wrapSvg(PATH_FILTER_ENABLE, color);
        case TimelineEventType::FilterDisable: return wrapSvg(PATH_FILTER_DISABLE, color);
        case TimelineEventType::HotkeyTrigger: return wrapSvg(PATH_HOTKEY, color);
        case TimelineEventType::TriggerTimeline: return wrapSvg(PATH_TRIGGER_TIMELINE, color);
        default: return wrapSvg("?", color);
    }
}

inline QColor getDefaultColor(TimelineEventType type) {
    switch (type) {
        case TimelineEventType::SceneSwitch:   return QColor("#4A90E2"); // Blue
        case TimelineEventType::SourceShow:    return QColor("#7ED321"); // Green
        case TimelineEventType::SourceHide:    return QColor("#D0021B"); // Red
        case TimelineEventType::MediaStart:    return QColor("#BD10E0"); // Purple
        case TimelineEventType::MediaStop:     return QColor("#F5A623"); // Orange
        case TimelineEventType::FilterEnable:  return QColor("#50E3C2"); // Teal
        case TimelineEventType::FilterDisable: return QColor("#9B9B9B"); // Grey
        case TimelineEventType::HotkeyTrigger: return QColor("#F8E71C"); // Yellow
        case TimelineEventType::TriggerTimeline: return QColor("#E91E63"); // Pink
        default: return Qt::gray;
    }
}

}
