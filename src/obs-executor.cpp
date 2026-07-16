#include "obs-executor.h"
#include "headless-timeline.h"
#include <obs-module.h>
#include <QFileInfo>
#include <obs-frontend-api.h>
#if OBS_TIMELINE_HAS_WEBSOCKET
#include <obs-websocket-api.h>
#endif

ObsExecutor::ObsExecutor(QObject *parent) : QObject(parent)
{
	restoreTimer = new QTimer(this);
	restoreTimer->setSingleShot(true);
	connect(restoreTimer, &QTimer::timeout, this, &ObsExecutor::restoreOriginalTransition);
	blog(LOG_INFO, "[Timeline] ├─ Executor created");
}

ObsExecutor::~ObsExecutor()
{
	cancelRestoreTimer();
	blog(LOG_INFO, "[Timeline] Executor destroyed");
}

void ObsExecutor::cancelRestoreTimer()
{
	if (restoreTimer)
		restoreTimer->stop();
	if (currentTemporaryTransition) {
		obs_source_release(currentTemporaryTransition);
		currentTemporaryTransition = nullptr;
	}
	if (originalTransition) {
		obs_source_release(originalTransition);
		originalTransition = nullptr;
	}
}

void ObsExecutor::restoreOriginalTransition()
{
	if (originalTransition) {
		obs_frontend_set_current_transition(originalTransition);
		obs_source_release(originalTransition);
	} else {
		obs_frontend_set_current_transition(nullptr);
	}
	originalTransition = nullptr;
	if (currentTemporaryTransition) {
		obs_source_release(currentTemporaryTransition);
		currentTemporaryTransition = nullptr;
	}
}

void ObsExecutor::setObsWebSocketVendor(void *vendor)
{
	this->obsWebSocketVendor = vendor;
}

void ObsExecutor::cancelAll()
{
	cancelRestoreTimer();
	blog(LOG_INFO, "[Timeline] cancelAll() called - stopping %d active nested timelines",
	     static_cast<int>(activeTimelines.size()));
	for (HeadlessTimeline *t : activeTimelines) {
		t->stop();
		t->deleteLater();
	}
	activeTimelines.clear();
}

void ObsExecutor::executeEventStart(const TimelineEvent &evt)
{
	performAction(evt, true);

	// WebSocket notification
#if OBS_TIMELINE_HAS_WEBSOCKET
	if (obsWebSocketVendor) {
		obs_data_t *event_data = obs_data_create();
		obs_data_set_string(event_data, "event", "started");
		obs_data_set_int(event_data, "type", static_cast<int>(evt.type));

		// We can't easily put QVariant into obs_data_t without conversion logic
		// For now just basic fields
		obs_data_set_string(event_data, "sourceName", evt.sourceName.toUtf8().constData());
		obs_data_set_double(event_data, "timeMs", evt.timeMs);

		obs_websocket_vendor_emit_event(obsWebSocketVendor, "TimelineEvent", event_data);
		obs_data_release(event_data);
	}
#endif
}

void ObsExecutor::executeEventEnd(const TimelineEvent &evt)
{
	performAction(evt, false);

#if OBS_TIMELINE_HAS_WEBSOCKET
	if (obsWebSocketVendor) {
		obs_data_t *event_data = obs_data_create();
		obs_data_set_string(event_data, "event", "ended");
		obs_data_set_int(event_data, "type", static_cast<int>(evt.type));
		obs_data_set_string(event_data, "sourceName", evt.sourceName.toUtf8().constData());
		obs_data_set_double(event_data, "timeMs", evt.timeMs);

		obs_websocket_vendor_emit_event(obsWebSocketVendor, "TimelineEvent", event_data);
		obs_data_release(event_data);
	}
#endif
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
		blog(LOG_WARNING, "[Timeline] └─ Unhandled event type: %d", static_cast<int>(evt.type));
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
			// obs_frontend_source_list_free() releases all sources in the list,
			// so we must addref the one we want to keep after returning.
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
		QString transitionName = evt.data.toString();
		obs_source_t *transition = nullptr;

		if (!transitionName.isEmpty()) {
			transition = find_transition(transitionName.toUtf8().constData());
			if (!transition) {
				blog(LOG_WARNING, "[Timeline] ┌─ Transition not found");
				blog(LOG_WARNING, "[Timeline] └─ Name : %s", transitionName.toUtf8().constData());
			}
		}

		if (obs_frontend_preview_program_mode_active())
			obs_frontend_set_current_preview_scene(scene);

		if (transition) {
			if (!originalTransition)
				originalTransition = obs_frontend_get_current_transition();
			if (currentTemporaryTransition)
				obs_source_release(currentTemporaryTransition);
			currentTemporaryTransition = transition;

			obs_frontend_set_current_transition(transition);

			const int durationMs = obs_frontend_get_transition_duration();
			const int restoreDelayMs = durationMs > 0 ? durationMs : 300;
			restoreTimer->start(restoreDelayMs);
		}

		obs_frontend_set_current_scene(scene);
		obs_source_release(scene);
		blog(LOG_INFO, "[Timeline] Scene switch → '%s' [%s]", evt.sourceName.toUtf8().constData(),
		     transitionName.isEmpty() ? "default" : transitionName.toUtf8().constData());
	} else {
		blog(LOG_WARNING, "[Timeline] ┌─ Scene not found");
		blog(LOG_WARNING, "[Timeline] └─ Name : %s", evt.sourceName.toUtf8().constData());
	}
}

void ObsExecutor::handleMediaStart(const TimelineEvent &evt)
{
	if (evt.sourceName.isEmpty())
		return;

	obs_source_t *source = obs_get_source_by_name(evt.sourceName.toUtf8().constData());
	if (source) {
		obs_source_media_restart(source);
		blog(LOG_INFO, "[Timeline] Media start → '%s'", evt.sourceName.toUtf8().constData());
		obs_source_release(source);
	} else {
		blog(LOG_WARNING, "[Timeline] ┌─ Media source not found");
		blog(LOG_WARNING, "[Timeline] └─ Name : %s", evt.sourceName.toUtf8().constData());
	}
}

void ObsExecutor::handleMediaStop(const TimelineEvent &evt)
{
	if (evt.sourceName.isEmpty())
		return;

	obs_source_t *source = obs_get_source_by_name(evt.sourceName.toUtf8().constData());
	if (source) {
		obs_source_media_stop(source);
		blog(LOG_INFO, "[Timeline] Media stop → '%s'", evt.sourceName.toUtf8().constData());
		obs_source_release(source);
	} else {
		blog(LOG_WARNING, "[Timeline] ┌─ Media source not found");
		blog(LOG_WARNING, "[Timeline] └─ Name : %s", evt.sourceName.toUtf8().constData());
	}
}

void ObsExecutor::handleSourceVisibility(const TimelineEvent &evt, bool visible)
{
	if (evt.sourceName.isEmpty())
		return;

	// Helper lambda to try setting visibility in a given scene source
	auto trySetVisibility = [&](obs_source_t *sceneSrc) -> bool {
		obs_scene_t *sc = obs_scene_from_source(sceneSrc);
		if (!sc)
			return false;
		obs_sceneitem_t *item = obs_scene_find_source(sc, evt.sourceName.toUtf8().constData());
		if (!item)
			return false;
		obs_sceneitem_set_visible(item, visible);
		blog(LOG_INFO, "[Timeline] Source '%s' → %s", evt.sourceName.toUtf8().constData(),
		     visible ? "visible" : "hidden");
		return true;
	};

	// Try current scene first
	obs_source_t *currentSceneSrc = obs_frontend_get_current_scene();
	if (currentSceneSrc) {
		bool found = trySetVisibility(currentSceneSrc);
		obs_source_release(currentSceneSrc);
		if (found)
			return;
	}

	// Fallback: search all scenes (can be expensive on large collections)
	struct obs_frontend_source_list list = {};
	obs_frontend_get_scenes(&list);
	bool foundAny = false;
	for (size_t i = 0; i < list.sources.num; i++) {
		if (trySetVisibility(list.sources.array[i])) {
			foundAny = true;
			break;
		}
	}
	obs_frontend_source_list_free(&list);

	if (!foundAny) {
		blog(LOG_WARNING, "[Timeline] ┌─ Source not found");
		blog(LOG_WARNING, "[Timeline] └─ Name : %s", evt.sourceName.toUtf8().constData());
	}
}

void ObsExecutor::handleFilterEnable(const TimelineEvent &evt, bool enable)
{
	if (evt.sourceName.isEmpty() || evt.filterName.isEmpty()) {
		blog(LOG_WARNING, "[Timeline] ┌─ Filter event incomplete");
		blog(LOG_WARNING, "[Timeline] └─ Missing source or filter name");
		return;
	}

	QString sourceName = evt.sourceName;
	QString filterName = evt.filterName;

	obs_source_t *source = obs_get_source_by_name(sourceName.toUtf8().constData());
	if (source) {
		obs_source_t *filter = obs_source_get_filter_by_name(source, filterName.toUtf8().constData());
		if (filter) {
			obs_source_set_enabled(filter, enable);
			blog(LOG_INFO, "[Timeline] Filter '%s' on '%s' → %s", filterName.toUtf8().constData(),
			     sourceName.toUtf8().constData(), enable ? "enabled" : "disabled");
			obs_source_release(filter);
		} else {
			blog(LOG_WARNING, "[Timeline] ┌─ Filter not found on source");
			blog(LOG_WARNING, "[Timeline] ├─ Source : %s", sourceName.toUtf8().constData());
			blog(LOG_WARNING, "[Timeline] └─ Filter : %s", filterName.toUtf8().constData());
		}
		obs_source_release(source);
	} else {
		blog(LOG_WARNING, "[Timeline] ┌─ Source not found for filter operation");
		blog(LOG_WARNING, "[Timeline] └─ Name : %s", sourceName.toUtf8().constData());
	}
}

void ObsExecutor::handleHotkey(const TimelineEvent &evt)
{
	if (evt.sourceName.isEmpty())
		return;

	// targetId contains the hotkey name, now stored in sourceName
	QString hotkeyName = evt.sourceName;

	struct HotkeySearch {
		QString targetName;
		obs_hotkey_id foundId = OBS_INVALID_HOTKEY_ID;
	};
	HotkeySearch search;
	search.targetName = hotkeyName;

	// Collect the hotkey ID first, trigger after enumeration completes
	// (triggering during enumeration may mutate the hotkey data structure)
	obs_enum_hotkeys(
		[](void *data, obs_hotkey_id id, obs_hotkey_t *hotkey) {
			HotkeySearch *s = static_cast<HotkeySearch *>(data);
			const char *name = obs_hotkey_get_name(hotkey);

			if (name && QString::fromUtf8(name) == s->targetName) {
				s->foundId = id;
				return false; // Stop enumeration
			}
			return true; // Continue enumeration
		},
		&search);

	if (search.foundId != OBS_INVALID_HOTKEY_ID) {
		obs_hotkey_trigger_routed_callback(search.foundId, true);
		obs_hotkey_trigger_routed_callback(search.foundId, false);
		blog(LOG_INFO, "[Timeline] Hotkey → '%s' (triggered)", hotkeyName.toUtf8().constData());
	}
}

void ObsExecutor::handleTriggerTimeline(const TimelineEvent &evt)
{
	QString filePath = evt.data.toString();
	if (filePath.isEmpty()) {
		// Fallback: Check if sourceName was used for filename (legacy/alternative)
		filePath = evt.sourceName;
	}

	if (filePath.isEmpty()) {
		blog(LOG_WARNING, "[Timeline] ┌─ TriggerTimeline failed");
		blog(LOG_WARNING, "[Timeline] └─ No file path specified");
		return;
	}

	// Prevent recursive execution (normalize path to handle symlinks and case differences)
	QString canonicalPath = QFileInfo(filePath).canonicalFilePath();
	for (auto *t : activeTimelines) {
		if (QFileInfo(t->getFilePath()).canonicalFilePath() == canonicalPath) {
			blog(LOG_WARNING, "[Timeline] ┌─ Recursive timeline trigger prevented");
			blog(LOG_WARNING, "[Timeline] └─ Path : %s", canonicalPath.toUtf8().constData());
			return;
		}
	}

	// Create headless timeline
	HeadlessTimeline *timeline = new HeadlessTimeline(this);
	timeline->setFilePath(filePath);
	if (timeline->loadFromFile(filePath)) {
		activeTimelines.push_back(timeline);

		// Cleanup when done — use QPointer to guard against race with cancelAll()
		QPointer<HeadlessTimeline> safeTimeline = timeline;
		connect(timeline, &HeadlessTimeline::finished, this, [this, safeTimeline]() {
			if (!safeTimeline)
				return;
			auto it = std::find(activeTimelines.begin(), activeTimelines.end(), safeTimeline.data());
			if (it != activeTimelines.end()) {
				activeTimelines.erase(it);
				safeTimeline->deleteLater();
			}
			blog(LOG_INFO, "[Timeline] Nested timeline finished");
		});

		timeline->start();
		blog(LOG_INFO, "[Timeline] Nested timeline → '%s'", filePath.toUtf8().constData());
	} else {
		timeline->deleteLater(); // Consistent with cancelAll() ownership model
		blog(LOG_WARNING, "[Timeline] ┌─ Failed to load nested timeline");
		blog(LOG_WARNING, "[Timeline] └─ Path : %s", filePath.toUtf8().constData());
	}
}
