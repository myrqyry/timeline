#include <obs-module.h>
#include <obs-frontend-api.h>
#include <QMainWindow>
#include <QDockWidget>
#include <QAction>
#include <QPointer>
#include <QFile>
#include <QDir>
#include "timeline-dock.h"
#include "macro-strip-dock.h"
#if OBS_TIMELINE_HAS_WEBSOCKET
#include <obs-websocket-api.h>
#endif

static QPointer<TimelineDock> g_timelineDock;
static QPointer<MacroStripDock> g_macroStripDock;
static obs_frontend_event_cb g_macroStripEventCb = nullptr;

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("timeline", "en-US")

static QString macroDataPath()
{
	const QString dir = QDir::homePath() + "/.config/obs-studio/plugins/timeline/data";
	QDir().mkpath(dir);
	return dir + "/macro_buttons.json";
}

static void macroStripSaveCallback(obs_data_t *save_data, bool saving, void *private_data)
{
	(void)private_data;
	if (!g_macroStripDock)
		return;
	if (saving) {
		const QByteArray json = g_macroStripDock->toJsonText().toUtf8();
		obs_data_set_string(save_data, "timeline_macro_strip_json", json.constData());
	} else {
		const char *json = obs_data_get_string(save_data, "timeline_macro_strip_json");
		if (!json || !*json)
			return;
		QFile f(macroDataPath());
		if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
			f.write(json);
			f.close();
		}
	}
}

static void macroStripFrontendEvent(enum obs_frontend_event event, void *private_data)
{
	(void)private_data;
	if (g_macroStripDock)
		g_macroStripDock->onFrontendEvent(event);
}

bool obs_module_load(void)
{
	blog(LOG_INFO, "[Timeline] ╔══════════════════════════════════════╗");
	blog(LOG_INFO, "[Timeline] ║  Timeline for OBS                    ║");
	blog(LOG_INFO, "[Timeline] ╚══════════════════════════════════════╝");

	g_timelineDock = new TimelineDock();
	obs_frontend_add_dock_by_id("timeline_dock", "Timeline", g_timelineDock);

	g_macroStripDock = new MacroStripDock();
	obs_frontend_add_dock_by_id("timeline_macro_strip", "Macro Strip", g_macroStripDock);
	if (g_timelineDock) {
		g_macroStripDock->setObsExecutor(g_timelineDock->obsExecutor());
	}

	obs_frontend_add_save_callback(macroStripSaveCallback, nullptr);
	g_macroStripEventCb = macroStripFrontendEvent;
	obs_frontend_add_event_callback(g_macroStripEventCb, nullptr);

	blog(LOG_INFO, "[Timeline] Runtime status:");
	blog(LOG_INFO, "[Timeline]  ├─ Timeline Dock    : registered");
	blog(LOG_INFO, "[Timeline]  ├─ Macro Strip Dock : registered");
	blog(LOG_INFO, "[Timeline]  └─ Status           : loaded");

	return true;
}

void obs_module_unload(void)
{
	if (g_macroStripEventCb) {
		obs_frontend_remove_event_callback(g_macroStripEventCb, nullptr);
		g_macroStripEventCb = nullptr;
	}
	obs_frontend_remove_save_callback(macroStripSaveCallback, nullptr);

	if (g_macroStripDock) {
		g_macroStripDock->saveNow();
		obs_frontend_remove_dock("timeline_macro_strip");
		g_macroStripDock->deleteLater();
	}
	g_macroStripDock = nullptr;

	if (g_timelineDock) {
		obs_frontend_remove_dock("timeline_dock");
		g_timelineDock->deleteLater();
	}
	g_timelineDock = nullptr;

	blog(LOG_INFO, "[Timeline] └─ Unloaded");
}

void obs_module_post_load(void)
{
#if OBS_TIMELINE_HAS_WEBSOCKET
	if (!g_timelineDock)
		return;

	obs_websocket_vendor vendor = obs_websocket_register_vendor("timeline");
	if (vendor) {
		blog(LOG_INFO, "[Timeline] ├─ WebSocket vendor : registered");
		g_timelineDock->setObsWebSocketVendor(vendor);
	} else {
		blog(LOG_WARNING, "[Timeline] └─ WebSocket vendor : registration failed");
	}
#else
	blog(LOG_INFO, "[Timeline] └─ WebSocket vendor : disabled at build time");
#endif

	if (g_macroStripDock) {
		g_macroStripDock->loadNow();
	}
}
