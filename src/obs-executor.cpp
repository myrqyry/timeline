#include "obs-executor.h"
#include "headless-timeline.h"
#include <obs-module.h>
#include <QTimer>
#include <obs-module.h>
#include <obs-frontend-api.h>
#include <obs-websocket-api.h>

ObsExecutor::ObsExecutor(QObject *parent) : QObject(parent)
{
	blog(LOG_INFO, "ObsExecutor created");
}

void ObsExecutor::setObsWebSocketVendor(void *vendor)
{
	this->obsWebSocketVendor = vendor;
}

void ObsExecutor::cancelAll()
{
	for (QTimer *t : activeTimers) {
		if (t->isActive()) {
			t->stop();
		}
		t->deleteLater();
	}
	activeTimers.clear();

	for (HeadlessTimeline *t : activeTimelines) {
		t->stop();
	}
}

void ObsExecutor::executeEvent(const TimelineEvent &evt)
{
	// 1. Perform the immediate "start" action
	performAction(evt, true);

	// 2. If it has a duration, schedule the "end" action
	if (evt.hasDuration && evt.durationMs > 0) {
		// Capture by value to ensure data exists when timer fires
		TimelineEvent endEvt = evt;
		int durationInt = static_cast<int>(evt.durationMs);
		QTimer *t = new QTimer(this);
		t->setSingleShot(true);
		connect(t, &QTimer::timeout, this, [this, endEvt, t]() {
			performAction(endEvt, false);
			blog(LOG_INFO, "Timeline: Duration ended for event '%s'",
			     endEvt.sourceName.toUtf8().constData());
			t->deleteLater();
			auto it = std::find(activeTimers.begin(), activeTimers.end(), t);
			if (it != activeTimers.end())
				activeTimers.erase(it);
		});
		activeTimers.push_back(t);
		t->start(durationInt);
	}

	// WebSocket notification
	if (obsWebSocketVendor) {
		obs_data_t *event_data = obs_data_create();
		obs_data_set_string(event_data, "event", "triggered");
		obs_data_set_int(event_data, "type", static_cast<int>(evt.type));

		// We can't easily put QVariant into obs_data_t without conversion logic
		// For now just basic fields
		obs_data_set_string(event_data, "sourceName", evt.sourceName.toUtf8().constData());
		obs_data_set_double(event_data, "timeMs", evt.timeMs);

		obs_websocket_vendor_emit_event(obsWebSocketVendor, "TimelineEvent", event_data);
		obs_data_release(event_data);
	}
}

void ObsExecutor::performAction(const TimelineEvent &evt, bool isStartAction)
{
	switch (evt.type) {
	case TimelineEventType::SourceShow:
	case TimelineEventType::SourceHide: {
		// If duration event: Start = visible, End = hidden
		// If instant event: SourceShow = visible, SourceHide = hidden
		bool visible = evt.hasDuration ? isStartAction : (evt.type == TimelineEventType::SourceShow);
		handleSourceVisibility(evt, visible);
		break;
	}
	case TimelineEventType::MediaStart:
	case TimelineEventType::MediaStop:
		if (evt.hasDuration) {
			if (isStartAction)
				handleMediaStart(evt);
			else
				handleMediaStop(evt);
		} else {
			if (evt.type == TimelineEventType::MediaStart)
				handleMediaStart(evt);
			else
				handleMediaStop(evt);
		}
		break;
	case TimelineEventType::FilterEnable:
	case TimelineEventType::FilterDisable: {
		// If duration event: Start = enable, End = disable
		// If instant event: FilterEnable = enable, FilterDisable = disable
		bool enable = evt.hasDuration ? isStartAction : (evt.type == TimelineEventType::FilterEnable);
		handleFilterEnable(evt, enable);
		break;
	}
	case TimelineEventType::SceneSwitch:
		// Only switch on start action (no inverse for scene switch)
		if (isStartAction)
			handleSceneSwitch(evt);
		break;
	case TimelineEventType::HotkeyTrigger:
		// Only trigger on start action
		if (isStartAction)
			handleHotkey(evt);
		break;
	case TimelineEventType::TriggerTimeline:
		// Only trigger on start action
		if (isStartAction)
			handleTriggerTimeline(evt);
		break;
	default:
		break;
	}
}

// Helper to find transition by name
static obs_source_t *find_transition(const char *name)
{
	if (!name || !*name)
		return nullptr;

	struct obs_frontend_source_list transitions = {};
	obs_frontend_get_transitions(&transitions);
	obs_source_t *found = nullptr;

	for (size_t i = 0; i < transitions.sources.num; i++) {
		obs_source_t *source = transitions.sources.array[i];
		const char *t_name = obs_source_get_name(source);
		if (t_name && strcmp(t_name, name) == 0) {
			// Found it. We need to increment the ref because we return it
			// and the list free will release it.
			// Wait, source list array[i] are likely references.
			// Calling free destroys the list struct but calls release on sources?
			// obs_frontend_source_list_free releases the sources.
			// So we need to addref if we want to keep it after list free.
			found = source;
			obs_source_get_ref(found);
			break;
		}
	}
	obs_frontend_source_list_free(&transitions);
	return found;
}

void ObsExecutor::handleSceneSwitch(const TimelineEvent &evt)
{
	if (evt.sourceName.isEmpty())
		return;

	obs_source_t *scene = obs_get_source_by_name(evt.sourceName.toUtf8().constData());
	if (scene) {
		// Check for specific transition
		QString transitionName = evt.data.toString();
		obs_source_t *transition = nullptr;
		obs_source_t *oldTransition = nullptr;

		if (!transitionName.isEmpty()) {
			transition = find_transition(transitionName.toUtf8().constData());
			if (transition) {
				oldTransition = obs_frontend_get_current_transition();
				obs_frontend_set_current_transition(transition);
				// Note: We don't own transition reference from get_source_by_name in a way we need to care about too much here?
				// obs_get_source_by_name increments ref. We should release later.
			} else {
				blog(LOG_WARNING, "Timeline: Transition '%s' not found",
				     transitionName.toUtf8().constData());
			}
		}

		obs_frontend_set_current_preview_scene(scene);
		obs_frontend_set_current_scene(scene);

		// Restore old transition?
		// Typically when you switch scene, the transition is used. If we change it back immediately,
		// it might race or apply to the *next* switch.
		// For now, let's leave it changed (user action). Or we could assume single-shot.
		// But OBS Studio Mode transition button sets it and leaves it.

		if (transition)
			obs_source_release(transition);
		if (oldTransition)
			obs_source_release(oldTransition);

		obs_source_release(scene); // FIX: Free scene pointer
		blog(LOG_INFO, "Timeline: Switched to scene '%s' using transition '%s'",
		     evt.sourceName.toUtf8().constData(),
		     transitionName.isEmpty() ? "(default)" : transitionName.toUtf8().constData());
	} else {
		blog(LOG_WARNING, "Timeline: Scene '%s' not found", evt.sourceName.toUtf8().constData());
	}
}

void ObsExecutor::handleMediaStart(const TimelineEvent &evt)
{
	if (evt.sourceName.isEmpty())
		return;

	obs_source_t *source = obs_get_source_by_name(evt.sourceName.toUtf8().constData());
	if (source) {
		obs_source_media_restart(source);
		blog(LOG_INFO, "Timeline: Started media '%s'", evt.sourceName.toUtf8().constData());
		obs_source_release(source);
	} else {
		blog(LOG_WARNING, "Timeline: Media source '%s' not found", evt.sourceName.toUtf8().constData());
	}
}

void ObsExecutor::handleMediaStop(const TimelineEvent &evt)
{
	if (evt.sourceName.isEmpty())
		return;

	obs_source_t *source = obs_get_source_by_name(evt.sourceName.toUtf8().constData());
	if (source) {
		obs_source_media_stop(source);
		blog(LOG_INFO, "Timeline: Stopped media '%s'", evt.sourceName.toUtf8().constData());
		obs_source_release(source);
	} else {
		blog(LOG_WARNING, "Timeline: Media source '%s' not found", evt.sourceName.toUtf8().constData());
	}
}

void ObsExecutor::handleSourceVisibility(const TimelineEvent &evt, bool visible)
{
	if (evt.sourceName.isEmpty())
		return;

	// Get current scene
	obs_source_t *currentSceneSrc = obs_frontend_get_current_scene();
	if (!currentSceneSrc) {
		blog(LOG_WARNING, "Timeline: No current scene");
		return;
	}

	obs_scene_t *scene = obs_scene_from_source(currentSceneSrc);
	if (scene) {
		obs_sceneitem_t *item = obs_scene_find_source(scene, evt.sourceName.toUtf8().constData());
		if (item) {
			obs_sceneitem_set_visible(item, visible);
			blog(LOG_INFO, "Timeline: Set source '%s' visibility to %s",
			     evt.sourceName.toUtf8().constData(), visible ? "visible" : "hidden");
		} else {
			blog(LOG_WARNING, "Timeline: Source '%s' not found in current scene",
			     evt.sourceName.toUtf8().constData());
		}
	}
	obs_source_release(currentSceneSrc);
}

void ObsExecutor::handleFilterEnable(const TimelineEvent &evt, bool enable)
{
	if (evt.sourceName.isEmpty() || evt.filterName.isEmpty()) {
		blog(LOG_WARNING, "Timeline: Filter event missing source or filter name");
		return;
	}

	QString sourceName = evt.sourceName;
	QString filterName = evt.filterName;

	obs_source_t *source = obs_get_source_by_name(sourceName.toUtf8().constData());
	if (source) {
		obs_source_t *filter = obs_source_get_filter_by_name(source, filterName.toUtf8().constData());
		if (filter) {
			obs_source_set_enabled(filter, enable);
			blog(LOG_INFO, "Timeline: Set filter '%s' on '%s' to %s", filterName.toUtf8().constData(),
			     sourceName.toUtf8().constData(), enable ? "enabled" : "disabled");
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

void ObsExecutor::handleHotkey(const TimelineEvent &evt)
{
	if (evt.sourceName.isEmpty())
		return;

	// targetId contains the hotkey name, now stored in sourceName
	QString hotkeyName = evt.sourceName;

	// Use obs_hotkey_inject_event to trigger the hotkey
	// First, we need to find the hotkey by name
	obs_enum_hotkeys(
		[](void *data, obs_hotkey_id id, obs_hotkey_t *hotkey) {
			QString *targetName = static_cast<QString *>(data);
			const char *name = obs_hotkey_get_name(hotkey);

			if (name && QString::fromUtf8(name) == *targetName) {
				// Trigger key down then key up
				obs_hotkey_trigger_routed_callback(id, true);  // Press
				obs_hotkey_trigger_routed_callback(id, false); // Release
				blog(LOG_INFO, "Timeline: Triggered hotkey '%s'", name);
				return false; // Stop enumeration
			}
			return true; // Continue enumeration
		},
		&hotkeyName);
}

void ObsExecutor::handleTriggerTimeline(const TimelineEvent &evt)
{
	QString filePath = evt.data.toString();
	if (filePath.isEmpty()) {
		// Fallback: Check if sourceName was used for filename (legacy/alternative)
		filePath = evt.sourceName;
	}

	if (filePath.isEmpty()) {
		blog(LOG_WARNING, "Timeline: TriggerTimeline event has no file path");
		return;
	}

	// Prevent recursive execution
	for (auto *t : activeTimelines) {
		if (t->getFilePath() == filePath) {
			blog(LOG_WARNING, "Timeline: Prevented recursive timeline trigger for '%s'",
			     filePath.toUtf8().constData());
			return;
		}
	}

	// Create headless timeline
	HeadlessTimeline *timeline = new HeadlessTimeline(this);
	timeline->setFilePath(filePath);
	if (timeline->loadFromFile(filePath)) {
		activeTimelines.push_back(timeline);

		// Cleanup when done
		connect(timeline, &HeadlessTimeline::finished, this, [this, timeline]() {
			auto it = std::find(activeTimelines.begin(), activeTimelines.end(), timeline);
			if (it != activeTimelines.end()) {
				activeTimelines.erase(it);
			}
			timeline->deleteLater();
			blog(LOG_INFO, "Timeline: Nested timeline finished");
		});

		timeline->start();
		blog(LOG_INFO, "Timeline: Started nested timeline '%s'", filePath.toUtf8().constData());
	} else {
		delete timeline;
		blog(LOG_WARNING, "Timeline: Failed to load nested timeline '%s'", filePath.toUtf8().constData());
	}
}
