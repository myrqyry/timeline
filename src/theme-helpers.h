#pragma once
#include <QColor>
#include <QString>
#include <QByteArray>
#include <QWidget>
#include "event-types.h"

// Material Design Icons paths (filled)
namespace ThemeIcons {

// Returns a good foreground/icon color for the given palette and state.
// Prefers WindowText / ButtonText / HighlightedText depending on context.
// Applies fallbacks so we almost never return a near-black on dark themes.
inline QColor iconColorFor(const QPalette &pal, bool onHighlight = false)
{
	QColor c;
	if (onHighlight) {
		c = pal.color(QPalette::HighlightedText);
	}
	if (!c.isValid() || c.lightness() < 55) {
		c = pal.color(QPalette::WindowText);
	}
	if (!c.isValid() || c.lightness() < 55) {
		c = pal.color(QPalette::Text);
	}
	if (!c.isValid() || c.lightness() < 55) {
		c = pal.color(QPalette::ButtonText);
	}
	if (!c.isValid() || c.lightness() < 55) {
		c = QColor(225, 225, 225); // safe light for dark OBS themes
	}
	return c;
}

// Convenience: resolve from a widget (polishes first for OBS theme accuracy).
inline QColor iconColorFor(const QWidget *w, bool onHighlight = false)
{
	if (!w) return QColor(225, 225, 225);
	const_cast<QWidget*>(w)->ensurePolished();
	return iconColorFor(w->palette(), onHighlight);
}

// SVG Wrapper template for filled icons
// Replaces currentColor and IconaMoon's hard-coded #292929 (and its stroke/fill attribute forms)
// so the baked result is always the requested live theme color for perfect OBS palette matching.
inline QByteArray wrapSvg(const QString &path, const QString &color)
{
	QString svg = QString("<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 24 24\" width=\"24\" height=\"24\" fill=\"%2\">%1</svg>")
		.arg(path, color);
	svg.replace("currentColor", color);
	svg.replace("#292929", color);
	svg.replace("stroke=\"#292929\"", QString("stroke=\"%1\"").arg(color));
	svg.replace("fill=\"#292929\"", QString("fill=\"%1\"").arg(color));
	return svg.toUtf8();
}

// Wrapper for IconaMoon Bold (stroke) icons — using the real official Bold SVGs from the IconaMoon set.
// Forces the provided color into the SVG (replaces currentColor + #292929 in attributes).
// This gives pixel-perfect IconaMoon Bold appearance while remaining fully theme-aware and readable.
inline QByteArray wrapIconMoon(const QString &inner, const QString &color)
{
	QString svg = QString("<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 24 24\" width=\"24\" height=\"24\" fill=\"none\" stroke=\"%2\" stroke-linecap=\"round\" stroke-linejoin=\"round\">%1</svg>")
		.arg(inner, color);
	svg.replace("currentColor", color);
	svg.replace("#292929", color);
	svg.replace("stroke=\"#292929\"", QString("stroke=\"%1\"").arg(color));
	svg.replace("fill=\"#292929\"", QString("fill=\"%1\"").arg(color));
	return svg.toUtf8();
}

inline const char *PATH_SCENE_SWITCH =
	"<path d=\"M4 6H2v14c0 1.1.9 2 2 2h14v-2H4V6zm16-4H8c-1.1 0-2 .9-2 2v12c0 1.1.9 2 2 2h12c1.1 0 2-.9 2-2V4c0-1.1-.9-2-2-2zm0 14H8V4h12v12z\"/>";
inline const char *PATH_SOURCE_SHOW =
	"<path d=\"M12 4.5C7 4.5 2.73 7.61 1 12c1.73 4.39 6 7.5 11 7.5s9.27-3.11 11-7.5c-1.73-4.39-6-7.5-11-7.5zM12 17c-2.76 0-5-2.24-5-5s2.24-5 5-5 5 2.24 5 5-2.24 5-5 5zm0-8c-1.66 0-3 1.34-3 3s1.34 3 3 3 3-1.34 3-3-1.34-3-3-3z\"/>";
inline const char *PATH_SOURCE_HIDE =
	"<path d=\"M12 7c2.76 0 5 2.24 5 5 0 .65-.13 1.26-.36 1.83l2.92 2.92c1.51-1.26 2.7-2.89 3.43-4.75-1.73-4.39-6-7.5-11-7.5-1.4 0-2.74.25-3.98.7l2.16 2.16C10.74 7.13 11.35 7 12 7zM2 4.27l2.28 2.28.46.46C3.08 8.3 1.78 10.02 1 12c1.73 4.39 6 7.5 11 7.5 1.55 0 3.03-.3 4.38-.88l.42.42 5.42 5.42 1.27-1.27L3.27 3 2 4.27zM7.53 9.8l1.55 1.55c-.05.21-.08.43-.08.65 0 1.66 1.34 3 3 3 .22 0 .44-.03.65-.08l1.55 1.55c-.67.33-1.41.53-2.2.53-2.76 0-5-2.24-5-5 0-.79.2-1.53.53-2.2zm4.31-.78l3.15 3.15.02-.16c0-1.66-1.34-3-3-3l-.17.01z\"/>";
inline const char *PATH_MEDIA_START = "<path d=\"M8 5v14l11-7z\"/>";
inline const char *PATH_MEDIA_STOP = "<path d=\"M6 6h12v12H6z\"/>";
inline const char *PATH_MENU = "<path d=\"M3 18h18v-2H3v2zm0-5h18v-2H3v2zm0-7v2h18V6H3z\"/>";
inline const char *PATH_PLAY = PATH_MEDIA_START;
inline const char *PATH_PAUSE = "<path d=\"M6 19h4V5H6v14zm8-14v14h4V5h-4z\"/>";
inline const char *PATH_STOP = PATH_MEDIA_STOP;
inline const char *PATH_PLUS = "<path d=\"M19 13h-6v6h-2v-6H5v-2h6V5h2v6h6v2z\"/>";
inline const char *PATH_MINUS = "<path d=\"M19 13H5v-2h14v2z\"/>";
inline const char *PATH_ADD_EVENT = "<path d=\"M19 13h-6v6h-2v-6H5v-2h6V5h2v6h6v2z\"/>";
inline const char *PATH_MAGNET =
	"<path d=\"M3 5v7c0 4.42 3.58 8 8 8s8-3.58 8-8V5h-5v7c0 1.66-1.34 3-3 3s-3-1.34-3-3V5H3zm2 2h1v5c0 2.76 2.24 5 5 5s5-2.24 5-5V7h1v5c0 3.31-2.69 6-6 6s-6-2.69-6-6V7z\"/>";
inline const char *PATH_REPEAT = "<path d=\"M7 7h11v3l4-4-4-4v3H5v6h2V7zm10 10H6v-3l-4 4 4 4v-3h13v-6h-2v4z\"/>";
inline const char *PATH_FILTER_ENABLE = "<path d=\"M9 16.17L4.83 12l-1.42 1.41L9 19 21 7l-1.41-1.41z\"/>";
inline const char *PATH_FILTER_DISABLE =
	"<path d=\"M19 6.41L17.59 5 12 10.59 6.41 5 5 6.41 10.59 12 5 17.59 6.41 19 12 13.41 17.59 19 19 17.59 13.41 12z\"/>";
inline const char *PATH_HOTKEY =
	"<path d=\"M20 5H4c-1.1 0-1.99.9-1.99 2L2 17c0 1.1.9 2 2 2h16c1.1 0 2-.9 2-2V7c0-1.1-.9-2-2-2zm-9 3h2v2h-2V8zm0 3h2v2h-2v-2zM8 8h2v2H8V8zm0 3h2v2H8v-2zm-1 2H5v-2h2v2zm0-3H5V8h2v2zm9 7H8v-2h8v2zm0-4h-2v-2h2v2zm0-3h-2V8h2v2z\"/>";
inline const char *PATH_TRIGGER_TIMELINE = "<path d=\"M4 10h12v2H4zm0-4h12v2H4zm0 8h8v2H4zm10 0v6l5-3z\"/>";

// === IconaMoon Bold (official Bold style from https://github.com/dariushhpg1/IconaMoon SVG/Bold ) ===
// Use wrapIconMoon() for these — they are thick-stroke, no-fill icons from the real set.
// All original hard-coded #292929 is replaced at runtime by the wrapper for theme awareness.

inline const char *MOON_PLAY =
	"<path d=\"M19 10.2679C20.3333 11.0377 20.3333 12.9623 19 13.7321L10 18.9282C8.66667 19.698 7 18.7358 7 17.1962L7 6.80385C7 5.26425 8.66667 4.302 10 5.0718L19 10.2679Z\"/>";

inline const char *MOON_PAUSE =
	"<path d=\"M5 7C5 5.89543 5.89543 5 7 5H8C9.10457 5 10 5.89543 10 7V17C10 18.1046 9.10457 19 8 19H7C5.89543 19 5 18.1046 5 17V7Z\"/>"
	"<path d=\"M14 7C14 5.89543 14.8954 5 16 5H17C18.1046 5 19 5.89543 19 7V17C19 18.1046 18.1046 19 17 19H16C14.8954 19 14 18.1046 14 17V7Z\"/>";

inline const char *MOON_STOP =
	"<rect x=\"5\" y=\"5\" width=\"14\" height=\"14\" rx=\"2\"/>";

inline const char *MOON_PLUS =
	"<path d=\"M12 6V18\"/>"
	"<path d=\"M6 12H18\"/>";

inline const char *MOON_MINUS =
	"<path d=\"M6 12H18\"/>";

// Clean three-line horizontal menu (Menu Burger Horizontal from IconaMoon Bold/Interface)
inline const char *MOON_MENU =
	"<g clip-path=\"url(#clip0)\">"
	"<path d=\"M3 6.00092H21M3 12.0009H21M3 18.0009H21\"/>"
	"</g>";

// Clean music note (Music 1 from IconaMoon Bold/Media)
inline const char *MOON_MUSIC =
	"<circle cx=\"9\" cy=\"18\" r=\"3\"/>"
	"<path d=\"M12 18V3\"/>"
	"<path d=\"M12 3L18 4V8L12 7V3Z\"/>";

// Simple clock face (Clock from IconaMoon Bold/General)
inline const char *MOON_CLOCK =
	"<circle cx=\"12\" cy=\"12\" r=\"9\"/>"
	"<path d=\"M11 8V13H16\"/>";

// Circular reload with arrows (Restart from IconaMoon Bold/Interface)
inline const char *MOON_RELOAD =
	"<g clip-path=\"url(#clip0)\">"
	"<path d=\"M12 2.99982C16.9706 2.99982 21 7.02925 21 11.9998C21 16.9704 16.9706 20.9998 12 20.9998C7.02944 20.9998 3 16.9704 3 11.9998C3 9.17255 4.30367 6.64977 6.34267 4.99982\"/>"
	"<path d=\"M3 4.49982H7V8.49982\"/>"
	"</g>";

// === IconaMoon Bold Numbers (real Number N.svg from IconaMoon Bold/Math) ===
inline const char *MOON_NUMBER_0_BOLD = "<path d=\"M12 20C14.7614 20 17 17.7614 17 15V9C17 6.23858 14.7614 4 12 4C9.23858 4 7 6.23858 7 9V15C7 17.7614 9.23858 20 12 20Z\"/>";
inline const char *MOON_NUMBER_1_BOLD = "<path d=\"M12 20V4L9 7\"/>";
inline const char *MOON_NUMBER_2_BOLD = "<path d=\"M8 8C8 6.97631 8.39052 5.95262 9.17157 5.17157C10.7337 3.60947 13.2663 3.60947 14.8284 5.17157C16.3905 6.73366 16.3905 9.26632 14.8284 10.8284L9.17157 16.4853C8.42143 17.2354 8 18.2528 8 19.3137L8 20L16 20\"/>";
inline const char *MOON_NUMBER_3_BOLD = "<path d=\"M8 19.0004C8.83566 19.6281 9.87439 20 11 20C13.7614 20 16 17.7614 16 15C16 12.2386 13.7614 10 11 10L16 4H8\"/>";
inline const char *MOON_NUMBER_4_BOLD = "<path d=\"M10 4L8.47845 11.6078C8.23093 12.8453 9.17752 14 10.4396 14H16M16 14V8M16 14V20\"/>";
inline const char *MOON_NUMBER_5_BOLD = "<path d=\"M8 19.0004C8.83566 19.6281 9.87439 20 11 20C13.7614 20 16 17.7614 16 15C16 12.2386 13.7614 10 11 10C9.87439 10 8.83566 10.3719 8 10.9996L9 4H16\"/>";
inline const char *MOON_NUMBER_6_BOLD =
	"<path d=\"M14.0587 4.6645C14.4257 4.07977 14.2492 3.30825 13.6645 2.94126C13.0798 2.57426 12.3083 2.75077 11.9413 3.3355L14.0587 4.6645Z\"/>"
	"<path d=\"M6.71438 11.6634C6.34739 12.2482 6.5239 13.0197 7.10863 13.3867C7.69335 13.7537 8.46488 13.5772 8.83187 12.9924L6.71438 11.6634Z\"/>"
	"<path d=\"M15.75 15C15.75 17.0711 14.0711 18.75 12 18.75V21.25C15.4518 21.25 18.25 18.4518 18.25 15H15.75ZM12 18.75C9.92893 18.75 8.25 17.0711 8.25 15H5.75C5.75 18.4518 8.54822 21.25 12 21.25V18.75ZM8.25 15C8.25 12.9289 9.92893 11.25 12 11.25V8.75C8.54822 8.75 5.75 11.5482 5.75 15H8.25ZM12 11.25C14.0711 11.25 15.75 12.9289 15.75 15H18.25C18.25 11.5482 15.4518 8.75 12 8.75V11.25ZM11.9413 3.3355L6.71438 11.6634L8.83187 12.9924L14.0587 4.6645L11.9413 3.3355Z\"/>";
inline const char *MOON_NUMBER_7_BOLD = "<path d=\"M8 4H16L10 20\"/>";
inline const char *MOON_NUMBER_8_BOLD =
	"<circle cx=\"12\" cy=\"15\" r=\"5\"/>"
	"<circle cx=\"12\" cy=\"7\" r=\"3\"/>";
inline const char *MOON_NUMBER_9_BOLD =
	"<path d=\"M9.94126 19.3355C9.57426 19.9202 9.75077 20.6917 10.3355 21.0587C10.9202 21.4257 11.6917 21.2492 12.0587 20.6645L9.94126 19.3355Z\"/>"
	"<path d=\"M17.2856 12.3366C17.6526 11.7518 17.4761 10.9803 16.8914 10.6133C16.3066 10.2463 15.5351 10.4228 15.1681 11.0076L17.2856 12.3366Z\"/>"
	"<path d=\"M8.25 9C8.25 6.92893 9.92893 5.25 12 5.25L12 2.75C8.54822 2.75 5.75 5.54822 5.75 9L8.25 9ZM12 5.25C14.0711 5.25 15.75 6.92893 15.75 9L18.25 9C18.25 5.54822 15.4518 2.75 12 2.75L12 5.25ZM15.75 9C15.75 11.0711 14.0711 12.75 12 12.75L12 15.25C15.4518 15.25 18.25 12.4518 18.25 9L15.75 9ZM12 12.75C9.92893 12.75 8.25 11.0711 8.25 9L5.75 9C5.75 12.4518 8.54822 15.25 12 15.25L12 12.75ZM12.0587 20.6645L17.2856 12.3366L15.1681 11.0076L9.94126 19.3355L12.0587 20.6645Z\"/>";

inline QByteArray getMoonNumberSvg(int digit, const QString &color)
{
	static const char* inners[10] = {
		MOON_NUMBER_0_BOLD, MOON_NUMBER_1_BOLD, MOON_NUMBER_2_BOLD, MOON_NUMBER_3_BOLD,
		MOON_NUMBER_4_BOLD, MOON_NUMBER_5_BOLD, MOON_NUMBER_6_BOLD, MOON_NUMBER_7_BOLD,
		MOON_NUMBER_8_BOLD, MOON_NUMBER_9_BOLD
	};
	if (digit < 0 || digit > 9) return {};
	return wrapIconMoon(inners[digit], color);
}

// Mode toggle icons (keep old filled for now, or we can swap to moon versions too)
inline const char *PATH_CLOCK =
	"<path d=\"M11.99 2C6.47 2 2 6.48 2 12s4.47 10 9.99 10C17.52 22 22 17.52 22 12S17.52 2 11.99 2zM12 20c-4.42 0-8-3.58-8-8s3.58-8 8-8 8 3.58 8 8-3.58 8-8 8zm.5-13H11v6l5.25 3.15.75-1.23-4.5-2.67z\"/>";
inline const char *PATH_MUSIC_NOTE =
	"<path d=\"M12 3v10.55c-.59-.34-1.27-.55-2-.55-2.21 0-4 1.79-4 4s1.79 4 4 4 4-1.79 4-4V7h4V3h-6z\"/>";

inline QByteArray getIconSvg(TimelineEventType type, const QString &color = "white")
{
	switch (type) {
	case TimelineEventType::SceneSwitch:
		return wrapSvg(PATH_SCENE_SWITCH, color);
	case TimelineEventType::SourceShow:
		return wrapSvg(PATH_SOURCE_SHOW, color);
	case TimelineEventType::SourceHide:
		return wrapSvg(PATH_SOURCE_HIDE, color);
	case TimelineEventType::MediaStart:
		return wrapSvg(PATH_MEDIA_START, color);
	case TimelineEventType::MediaStop:
		return wrapSvg(PATH_MEDIA_STOP, color);
	case TimelineEventType::FilterEnable:
		return wrapSvg(PATH_FILTER_ENABLE, color);
	case TimelineEventType::FilterDisable:
		return wrapSvg(PATH_FILTER_DISABLE, color);
	case TimelineEventType::HotkeyTrigger:
		return wrapSvg(PATH_HOTKEY, color);
	case TimelineEventType::TriggerTimeline:
		return wrapSvg(PATH_TRIGGER_TIMELINE, color);
	default:
		return wrapSvg("?", color);
	}
}

inline QColor getDefaultColor(TimelineEventType type)
{
	switch (type) {
	case TimelineEventType::SceneSwitch:
		return QColor("#4A90E2"); // Blue
	case TimelineEventType::SourceShow:
		return QColor("#7ED321"); // Green
	case TimelineEventType::SourceHide:
		return QColor("#D0021B"); // Red
	case TimelineEventType::MediaStart:
		return QColor("#BD10E0"); // Purple
	case TimelineEventType::MediaStop:
		return QColor("#F5A623"); // Orange
	case TimelineEventType::FilterEnable:
		return QColor("#50E3C2"); // Teal
	case TimelineEventType::FilterDisable:
		return QColor("#9B9B9B"); // Grey
	case TimelineEventType::HotkeyTrigger:
		return QColor("#F8E71C"); // Yellow
	case TimelineEventType::TriggerTimeline:
		return QColor("#E91E63"); // Pink
	default:
		return Qt::gray;
	}
}

} // namespace ThemeIcons
