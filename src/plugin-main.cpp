#include <obs-module.h>
#include <obs-frontend-api.h>
#include <QMainWindow>
#include <QDockWidget>
#include <QAction>
#include "timeline-dock.h"
#if OBS_TIMELINE_HAS_WEBSOCKET
#include <obs-websocket-api.h>
#endif

static TimelineDock *g_timelineDock = nullptr;

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("timeline", "en-US")

bool obs_module_load(void)
{
	g_timelineDock = new TimelineDock();
	obs_frontend_add_dock_by_id("timeline_dock", "Timeline", g_timelineDock);

	blog(LOG_INFO, "Timeline for OBS loaded successfully");
	return true;
}

void obs_module_unload(void)
{
	obs_frontend_remove_dock("timeline_dock");

	g_timelineDock = nullptr;
	blog(LOG_INFO, "Timeline for OBS unloaded");
}

void obs_module_post_load(void)
{
#if OBS_TIMELINE_HAS_WEBSOCKET
	if (!g_timelineDock)
		return;

	obs_websocket_vendor vendor = obs_websocket_register_vendor("timeline");
	if (vendor) {
		blog(LOG_INFO, "Registered as obs-websocket vendor");
		g_timelineDock->setObsWebSocketVendor(vendor);
	} else {
		blog(LOG_WARNING, "Failed to register as obs-websocket vendor");
	}
#else
	blog(LOG_INFO, "obs-websocket vendor support disabled at build time");
#endif
}
