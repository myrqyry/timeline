#include <obs-module.h>
#include <obs-frontend-api.h>
#include <QMainWindow>
#include <QDockWidget>
#include <QAction>
#include <QPointer>
#include "timeline-dock.h"
#include "plugin-support.h"
#if OBS_TIMELINE_HAS_WEBSOCKET
#include <obs-websocket-api.h>
#endif

static QPointer<TimelineDock> g_timelineDock;

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("timeline", "en-US")

bool obs_module_load(void)
{
	blog(LOG_INFO, "[Timeline] ╔══════════════════════════════════════╗");
	blog(LOG_INFO, "[Timeline] ║  Timeline for OBS v%s           ║", PLUGIN_VERSION);
	blog(LOG_INFO, "[Timeline] ╚══════════════════════════════════════╝");

#ifdef TIMELINE_DEBUG_ACTIVE_MARKER
	blog(LOG_INFO, "DEBUG_ACTIVE_TIMELINE_7B3E1");
#endif

	g_timelineDock = new TimelineDock();
	obs_frontend_add_dock_by_id("timeline_dock", "Timeline", g_timelineDock);

	blog(LOG_INFO, "[Timeline] Runtime status:");
	blog(LOG_INFO, "[Timeline]  ├─ Dock    : registered");
	blog(LOG_INFO, "[Timeline]  └─ Version : %s", PLUGIN_VERSION);

	return true;
}

void obs_module_unload(void)
{
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
}
