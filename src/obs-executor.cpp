#include "obs-executor.h"
#include <obs-module.h>
#include <obs-frontend-api.h>

ObsExecutor::ObsExecutor(QObject *parent) : QObject(parent) {
    blog(LOG_INFO, "ObsExecutor created");
}

void ObsExecutor::executeEvent(const TimelineEvent &evt) {
    switch (evt.type) {
        case TimelineEventType::SceneSwitch:
            handleSceneSwitch(evt);
            break;
        case TimelineEventType::MediaStart:
            handleMediaStart(evt);
            break;
        case TimelineEventType::MediaStop:
            handleMediaStop(evt);
            break;
        case TimelineEventType::SourceShow:
            handleSourceVisibility(evt, true);
            break;
        case TimelineEventType::SourceHide:
            handleSourceVisibility(evt, false);
            break;
        case TimelineEventType::FilterEnable:
            handleFilterEnable(evt, true);
            break;
        case TimelineEventType::FilterDisable:
            handleFilterEnable(evt, false);
            break;
        case TimelineEventType::HotkeyTrigger:
            handleHotkey(evt);
            break;
        default:
            blog(LOG_WARNING, "Timeline: Unknown event type");
            break;
    }
}

void ObsExecutor::handleSceneSwitch(const TimelineEvent &evt) {
    if (evt.targetId.isEmpty()) return;
    
    obs_source_t *scene = obs_get_source_by_name(evt.targetId.toUtf8().constData());
    if (scene) {
        obs_frontend_set_current_preview_scene(scene);
        obs_frontend_set_current_scene(scene);
        obs_source_release(scene);
        blog(LOG_INFO, "Timeline: Switched to scene '%s'", evt.targetId.toUtf8().constData());
    } else {
        blog(LOG_WARNING, "Timeline: Scene '%s' not found", evt.targetId.toUtf8().constData());
    }
}

void ObsExecutor::handleMediaStart(const TimelineEvent &evt) {
    if (evt.targetId.isEmpty()) return;
    
    obs_source_t *source = obs_get_source_by_name(evt.targetId.toUtf8().constData());
    if (source) {
        obs_source_media_restart(source);
        blog(LOG_INFO, "Timeline: Started media '%s'", evt.targetId.toUtf8().constData());
        obs_source_release(source);
    } else {
        blog(LOG_WARNING, "Timeline: Media source '%s' not found", evt.targetId.toUtf8().constData());
    }
}

void ObsExecutor::handleMediaStop(const TimelineEvent &evt) {
    if (evt.targetId.isEmpty()) return;
    
    obs_source_t *source = obs_get_source_by_name(evt.targetId.toUtf8().constData());
    if (source) {
        obs_source_media_stop(source);
        blog(LOG_INFO, "Timeline: Stopped media '%s'", evt.targetId.toUtf8().constData());
        obs_source_release(source);
    } else {
        blog(LOG_WARNING, "Timeline: Media source '%s' not found", evt.targetId.toUtf8().constData());
    }
}

void ObsExecutor::handleSourceVisibility(const TimelineEvent &evt, bool visible) {
    if (evt.targetId.isEmpty()) return;
    
    // Get current scene
    obs_source_t *currentSceneSrc = obs_frontend_get_current_scene();
    if (!currentSceneSrc) {
        blog(LOG_WARNING, "Timeline: No current scene");
        return;
    }
    
    obs_scene_t *scene = obs_scene_from_source(currentSceneSrc);
    if (scene) {
        obs_sceneitem_t *item = obs_scene_find_source(scene, evt.targetId.toUtf8().constData());
        if (item) {
            obs_sceneitem_set_visible(item, visible);
            blog(LOG_INFO, "Timeline: Set source '%s' visibility to %s", 
                 evt.targetId.toUtf8().constData(), visible ? "visible" : "hidden");
        } else {
            blog(LOG_WARNING, "Timeline: Source '%s' not found in current scene", 
                 evt.targetId.toUtf8().constData());
        }
    }
    obs_source_release(currentSceneSrc);
}

void ObsExecutor::handleFilterEnable(const TimelineEvent &evt, bool enable) {
    if (evt.targetId.isEmpty()) return;
    
    // targetId format: "SourceName::FilterName"
    QStringList parts = evt.targetId.split("::");
    if (parts.size() != 2) {
        blog(LOG_WARNING, "Timeline: Invalid filter target format '%s' (expected 'Source::Filter')", 
             evt.targetId.toUtf8().constData());
        return;
    }
    
    QString sourceName = parts[0];
    QString filterName = parts[1];
    
    obs_source_t *source = obs_get_source_by_name(sourceName.toUtf8().constData());
    if (source) {
        obs_source_t *filter = obs_source_get_filter_by_name(source, filterName.toUtf8().constData());
        if (filter) {
            obs_source_set_enabled(filter, enable);
            blog(LOG_INFO, "Timeline: Set filter '%s' on '%s' to %s", 
                 filterName.toUtf8().constData(), sourceName.toUtf8().constData(),
                 enable ? "enabled" : "disabled");
            obs_source_release(filter);
        } else {
            blog(LOG_WARNING, "Timeline: Filter '%s' not found on source '%s'", 
                 filterName.toUtf8().constData(), sourceName.toUtf8().constData());
        }
        obs_source_release(source);
    } else {
        blog(LOG_WARNING, "Timeline: Source '%s' not found for filter operation", 
             sourceName.toUtf8().constData());
    }
}

void ObsExecutor::handleHotkey(const TimelineEvent &evt) {
    if (evt.targetId.isEmpty()) return;
    
    // targetId contains the hotkey name
    QString hotkeyName = evt.targetId;
    
    // Use obs_hotkey_inject_event to trigger the hotkey
    // First, we need to find the hotkey by name
    obs_enum_hotkeys([](void *data, obs_hotkey_id id, obs_hotkey_t *hotkey) {
        QString *targetName = static_cast<QString*>(data);
        const char *name = obs_hotkey_get_name(hotkey);
        
        if (name && QString::fromUtf8(name) == *targetName) {
            // Trigger key down then key up
            obs_hotkey_trigger_routed_callback(id, true);  // Press
            obs_hotkey_trigger_routed_callback(id, false); // Release
            blog(LOG_INFO, "Timeline: Triggered hotkey '%s'", name);
            return false; // Stop enumeration
        }
        return true; // Continue enumeration
    }, &hotkeyName);
}
