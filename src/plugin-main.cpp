#include <obs-module.h>
#include <obs-frontend-api.h>
#include <QMainWindow>
#include <QDockWidget>
#include <QAction>
#include "timeline-dock.h"

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("obs-timeline-plugin", "en-US")

bool obs_module_load(void)
{
    TimelineDock *dock = new TimelineDock();
    obs_frontend_add_dock_by_id("timeline_dock", "Timeline", dock);
    
    blog(LOG_INFO, "OBS Timeline Plugin loaded successfully");
    return true;
}

void obs_module_unload(void)
{
    blog(LOG_INFO, "OBS Timeline Plugin unloaded");
}
