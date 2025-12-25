#include <obs-module.h>
#include <obs-frontend-api.h>
#include <QMainWindow>
#include <QDockWidget>
#include <QAction>
#include "timeline-dock.h"
#include <obs-websocket-api.h>

static TimelineDock *g_timelineDock = nullptr;

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("obs-timeline-plugin", "en-US")

bool obs_module_load(void)
{
    g_timelineDock = new TimelineDock();
    obs_frontend_add_dock_by_id("timeline_dock", "Timeline", g_timelineDock);
    
    blog(LOG_INFO, "OBS Timeline Plugin loaded successfully");
    return true;
}

void obs_module_unload(void)
{
    blog(LOG_INFO, "OBS Timeline Plugin unloaded");
}

void obs_module_post_load(void)
{
    if (!g_timelineDock) return;
    
    // Register as obs-websocket vendor
    obs_websocket_vendor vendor = obs_websocket_register_vendor("obs-timeline-plugin");
    if (vendor) {
        blog(LOG_INFO, "Registered as obs-websocket vendor");
        g_timelineDock->setObsWebSocketVendor(vendor);
    } else {
        blog(LOG_WARNING, "Failed to register as obs-websocket vendor (obs-websocket possibly not loaded)");
    }
}
