#include "obs-executor.h"
#include <obs-module.h>

ObsExecutor::ObsExecutor(QObject *parent) : QObject(parent) {
    blog(LOG_INFO, "ObsExecutor created");
}

void ObsExecutor::executeEvent(const TimelineEvent &evt) {
    // Dispatch to main thread to be safe with OBS API
    // struct Data { TimelineEvent evt; ObsExecutor* self; };
    // But simpler is to use the frontend API's queue if accessed from non-UI thread.
    // However, our QTimer is likely on the main UI thread if created there.
    // Assuming Main Thread execution for now since QTimer in plugin-main is on main thread.
    
    switch (evt.type) {
        case TimelineEventType::SceneSwitch:
            handleSceneSwitch(evt);
            break;
        default:
            break;
    }
}

void ObsExecutor::handleSceneSwitch(const TimelineEvent &evt) {
    if (evt.targetId.isEmpty()) return;
    
    // Find scene source
    obs_source_t *scene = obs_get_source_by_name(evt.targetId.toUtf8().constData());
    if (scene) {
        obs_frontend_set_current_preview_scene(scene);
        obs_frontend_set_current_scene(scene);
        obs_source_release(scene);
    } else {
        blog(LOG_WARNING, "Timeline: Scene '%s' not found", evt.targetId.toUtf8().constData());
    }
}
