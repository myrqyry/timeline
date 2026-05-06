# OBS Timeline Plugin — Technical Architecture Reference

> **Document Version**: 1.0  
> **Last Updated**: 2026-05-05  
> **Scope**: Comprehensive technical reference for developers, architects, and maintainers  

---

## Table of Contents

1. [Executive Summary](#1-executive-summary)
2. [Architecture Overview](#2-architecture-overview)
3. [Design Decisions & Rationale](#3-design-decisions--rationale)
4. [Core Components](#4-core-components)
5. [Data Models](#5-data-models)
6. [Integration Points](#6-integration-points)
7. [Deployment Architecture](#7-deployment-architecture)
8. [Performance Characteristics](#8-performance-characteristics)
9. [Security Model](#9-security-model)
10. [Appendices](#10-appendices)

---

## 1. Executive Summary

The **OBS Timeline Plugin** is a dockable frontend plugin for OBS Studio that provides a visual timeline interface for automating scene switches, source visibility toggles, media playback control, filter enablement, hotkey triggers, and nested timeline execution. It bridges Qt's widget framework with OBS's C API to deliver frame-precise scheduling capabilities during live productions.

**Key Capabilities**:
- Visual timeline with draggable event nodes (`QGraphicsView`/`QGraphicsScene`)
- 8 event types covering the majority of live-production automation needs
- BPM-grid snapping and configurable snap intervals
- Looping playback with end-time boundaries
- Nested timeline triggers for complex multi-file sequences
- obs-websocket vendor integration for external control
- Scene-collection-bound persistence via OBS save callbacks

**Technology Stack**:
- **Language**: C++17
- **Build System**: CMake 3.16+
- **UI Framework**: Qt6 Widgets + QGraphicsView
- **Target Platform**: OBS Studio Plugin API (libobs)
- **Optional Integration**: obs-websocket vendor API

---

## 2. Architecture Overview

### 2.1 System Boundaries

The plugin operates entirely within the OBS Studio process space as a frontend plugin. It does not spawn external processes, network listeners (except via the optional websocket vendor), or GPU compute kernels.

```
+-------------------------------------------------------------+
|                    OBS Studio Process                        |
|  +-------------------------------------------------------+  |
|  |             OBS Frontend Thread (Qt UI)               |  |
|  |  +-------------+  +--------------+  +-------------+  |  |
|  |  | TimelineDock|  |TimelineModel |  |  Scheduler  |  |  |
|  |  |  (QWidget)  |  |  (Data)      |  |  (QTimer)   |  |  |
|  |  +------+------+  +------+-------+  +------+------+  |  |
|  |         |                |                 |         |  |
|  |  +------v----------------v-----------------v------+  |  |
|  |  |              ObsExecutor                        |  |  |
|  |  |         (OBS C API Bridge)                      |  |  |
|  |  +------------------+-----------------------------+  |  |
|  |                     |                                 |  |
|  |  +------------------v-----------------------------+  |  |
|  |  |           libobs / obs-frontend-api            |  |  |
|  |  +------------------------------------------------+  |  |
|  +-------------------------------------------------------+  |
+-------------------------------------------------------------+
```

### 2.2 Component Interaction Flow

**Data Flow — Playback**:
1. `TimelineScheduler::onTimerTick()` (16ms interval) queries current playback time
2. Scheduler compares time against sorted `TimelineModel::events`
3. When `event.timeMs <= currentTime`, scheduler emits `eventStarted(evt)`
4. `ObsExecutor::executeEventStart()` receives signal and dispatches to OBS API
5. Executor performs the action (scene switch, source visibility, etc.)
6. Optional: websocket vendor emits event for external consumers

**Data Flow — User Edit**:
1. User drags `TimelineEventItem` on the `QGraphicsScene`
2. Item emits `eventMoved(newTimeMs)` via `itemChange()`
3. `TimelineDock` updates model via `model->updateEventTimeById()`
4. Model sorts events and emits `modelChanged()`
5. `TimelineDock` queues `rebuildEventItems()` via `Qt::QueuedConnection`
6. UI rebuilds graphics items from model (coalesced to avoid redundant work)

### 2.3 Layered Architecture

| Layer | Components | Responsibility |
|-------|-----------|--------------|
| **Presentation** | `TimelineDock`, `TimelineEventItem`, `EventEditor` | Qt UI, graphics rendering, input handling |
| **Application** | `TimelineScheduler`, `HeadlessTimeline` | Playback timing, event firing, loop logic |
| **Domain** | `TimelineModel`, `TimelineEvent`, `TimelineEventType` | Data structures, business rules, sorting |
| **Infrastructure** | `ObsExecutor`, `TimelineSerialization` | OBS API calls, JSON persistence, websocket |

---

## 3. Design Decisions & Rationale

### 3.1 Model-View Separation with Qt Signals

**Decision**: Use `TimelineModel` as a pure data container emitting `modelChanged()` rather than tightly coupling data mutations to UI updates.

**Rationale**:
- Enables the `HeadlessTimeline` to reuse the same model/scheduler stack without UI dependencies
- Allows `QueuedConnection` coalescing: rapid model mutations (nudge, paste) batch into a single rebuild
- Simplifies testing: model operations can be unit-tested without instantiating Qt graphics objects

**Trade-off**: Adds one signal-slot indirection. The `rebuildScheduled` flag in `TimelineDock` prevents queue saturation.

### 3.2 Index-Decoupled Fired-State Tracking

**Decision**: Store `eventStartFired` and `eventEndFired` as `QSet<QString>` keyed by event UUID rather than parallel `std::vector<bool>` indexed by position.

**Rationale**:
- `sortEvents()` reorders the vector after every mutation (add, time update, replace, JSON load)
- Index-coupled state caused events to re-fire or skip after sorts because index *i* no longer mapped to the same event
- UUIDs are generated at creation (`QUuid::createUuid()`) and immutable, providing a stable identity

**Trade-off**: Slightly higher memory overhead (QString hashes vs. packed bools) and O(log n) lookup vs. O(1) array access. For typical timelines (< 1000 events), this is negligible.

### 3.3 Direct `delete` for QGraphicsItems

**Decision**: Use `delete` instead of `deleteLater()` when removing `TimelineEventItem` instances from the scene during rebuilds.

**Rationale**:
- `scene->removeItem()` already unparents the item from the scene graph
- `deleteLater()` defers deletion to the next event loop iteration, creating a window where the raw pointer in `eventItemMap` is dangling
- Rapid rebuilds (paste of many events, model reset) could trigger use-after-free if the map was iterated before deferred deletion

**Trade-off**: Requires ensuring no event is currently being processed for the item. The `isDragging` guard and queued rebuild mechanism provide this guarantee.

### 3.4 Single-Threaded Execution Model

**Decision**: All OBS API calls occur on the Qt UI thread (same as OBS frontend thread).

**Rationale**:
- OBS frontend API (`obs_frontend_set_current_scene`, `obs_sceneitem_set_visible`, etc.) is not thread-safe and must be called from the main thread
- The scheduler's `QTimer` lives on the UI thread, and signals are `Qt::DirectConnection` by default within the same thread
- `HeadlessTimeline` is parented to `ObsExecutor` (a UI-thread object), ensuring its scheduler also runs on the UI thread

**Trade-off**: Heavy timelines could block UI briefly during event bursts. Mitigated by the fact that OBS actions are generally fast (source lookups, flag sets).

### 3.5 Scene-Collection-Bound Persistence

**Decision**: Use `obs_frontend_add_save_callback` to persist timeline state into OBS's config directory rather than a standalone file.

**Rationale**:
- Timeline state is logically bound to the current scene collection (events reference scenes and sources by name)
- Automatic save on OBS exit or scene collection switch — no manual "Save" button required for normal operation
- Import/Export to JSON files remains available for backup and sharing

**Trade-off**: Users cannot have timeline state independent of scene collections. This is considered desirable behavior.

---

## 4. Core Components

### 4.1 TimelineModel

**Files**: `src/timeline-model.h`, `src/timeline-model.cpp`

**Purpose**: In-memory data store for timeline events. Owns the canonical event list and enforces ordering.

**Public Interface**:

```cpp
class TimelineModel : public QObject {
    void addEvent(const TimelineEvent &event);
    const std::vector<TimelineEvent>& getEvents() const;
    void clearEvents();
    int findEventIndexById(const QString &id) const;
    bool removeEventById(const QString &id);
    bool updateEventTimeById(const QString &id, double newTimeMs);
    bool replaceEventById(const QString &id, const TimelineEvent &event);
    const TimelineEvent *findEventById(const QString &id) const;
    QJsonArray toJson() const;
    void fromJson(const QJsonArray &json);
    void sortEvents();
signals:
    void modelChanged();
};
```

**Key Behaviors**:
- `addEvent()`, `updateEventTimeById()`, `replaceEventById()`, and `fromJson()` all call `sortEvents()` followed by `emit modelChanged()`
- Sorting is by `timeMs` ascending using `std::sort`
- `findEventIndexById()` performs a linear scan (O(n)). For large timelines, this could be optimized to a hash map, but n is typically small

**Thread Safety**: Not thread-safe. All access must occur on the owner's thread (Qt UI thread).

### 4.2 TimelineScheduler

**Files**: `src/timeline-scheduler.h`, `src/timeline-scheduler.cpp`

**Purpose**: Drives playback time and fires events at their scheduled moments.

**Time Tracking Strategy**:
- Uses `std::chrono::high_resolution_clock` to measure elapsed wall-clock time
- Stores `currentOffsetMs`: the timeline position at the moment `start()` was last called
- `getCurrentTime()` = `currentOffsetMs + elapsedSinceStart`
- `seek()` resets both `currentOffsetMs` and the `startTime` anchor

**Event Firing Logic**:

```cpp
void TimelineScheduler::onTimerTick() {
    double currentTime = getCurrentTime();
    // ... end-time / looping checks ...
    
    for (const TimelineEvent &evt : events) {
        if (!eventStartFired.contains(evt.id) && evt.timeMs <= currentTime) {
            emit eventStarted(evt);
            eventStartFired.insert(evt.id);
        }
        if (evt.hasDuration && evt.durationMs > 0) {
            const double endMs = evt.timeMs + evt.durationMs;
            if (!eventEndFired.contains(evt.id) && endMs <= currentTime) {
                emit eventEnded(evt);
                eventEndFired.insert(evt.id);
            }
        }
    }
}
```

**Design Notes**:
- `eventStartFired` / `eventEndFired` are `QSet<QString>` keyed by event ID (see section 3.2)
- On `modelChanged`, fired state is recomputed by scanning all events against current time
- On loop, all fired state is cleared via `seek(0)` followed by set clears

### 4.3 ObsExecutor

**Files**: `src/obs-executor.h`, `src/obs-executor.cpp`

**Purpose**: Bridge between timeline events and OBS C API actions.

**Action Dispatch Table**:

| Event Type | Start Action | End Action |
|-----------|-------------|-----------|
| `SceneSwitch` | `obs_frontend_set_current_scene()` | No-op |
| `SourceShow` / `SourceHide` | `obs_sceneitem_set_visible(true/false)` | Inverted if duration event |
| `MediaStart` / `MediaStop` | `obs_source_media_restart()` / `media_stop()` | Inverted if duration event |
| `FilterEnable` / `FilterDisable` | `obs_source_set_enabled(filter, true/false)` | Inverted if duration event |
| `HotkeyTrigger` | `obs_hotkey_trigger_routed_callback()` | No-op |
| `TriggerTimeline` | Spawn `HeadlessTimeline` | No-op |

**Scene Switch with Custom Transition**:

```cpp
obs_source_t *transition = find_transition(transitionName);
if (transition) {
    oldTransition = obs_frontend_get_current_transition();
    obs_frontend_set_current_transition(transition);
}
obs_frontend_set_current_scene(scene);
// Transition is left as-set (not restored) to avoid races
```

**Source Visibility — Cross-Scene Fallback**:

```cpp
// Try current scene first
obs_source_t *cur = obs_frontend_get_current_scene();
if (cur) {
    bool found = trySetVisibility(cur);
    obs_source_release(cur);
    if (found) return;
}

// Fallback: search all scenes
struct obs_frontend_source_list list = {};
obs_frontend_get_scenes(&list);
for (size_t i = 0; i < list.sources.num; i++) {
    if (trySetVisibility(list.sources.array[i])) break;
}
obs_frontend_source_list_free(&list);
```

**Nested Timeline Execution**:
- `handleTriggerTimeline()` creates a `HeadlessTimeline` instance
- Recursive execution is prevented by comparing canonical file paths
- Active timelines are tracked in `std::vector<HeadlessTimeline *> activeTimelines`
- On `finished` signal, the timeline is removed from the active list and deleted

### 4.4 TimelineDock

**Files**: `src/timeline-dock.h`, `src/timeline-dock.cpp`

**Purpose**: Main UI widget. Owns model, scheduler, and executor. Manages `QGraphicsView` scene, controls, and persistence.

**UI Architecture**:

```
TimelineDock (QWidget)
+-- QVBoxLayout
    +-- controlsLayout (QHBoxLayout)
    |   +-- playButton, stopButton, zoomInButton, zoomOutButton
    |   +-- addEventButton, saveButton, loadButton
    |   +-- loopCheckbox, snapCheckbox, centerControlsCheckbox
    |   +-- endTimeSpinbox, bpmSpinbox, snapIntervalSpinbox
    |   +-- timeLabel, endLabel, snapIntervalLabel
    |   +-- bpmGridCheckbox
    +-- QGraphicsView
        +-- QGraphicsScene
            +-- Ruler lines (1-second ticks)
            +-- Grid lines (optional BPM grid)
            +-- Playhead line (red)
            +-- End marker line (green)
            +-- Empty hint text
            +-- TimelineEventItem nodes
```

**Coalesced Rebuild Mechanism**:

```cpp
connect(model, &TimelineModel::modelChanged, this, [this]() {
    if (!rebuildScheduled) {
        rebuildScheduled = true;
        QMetaObject::invokeMethod(this, &TimelineDock::rebuildEventItems, 
                                  Qt::QueuedConnection);
    }
});
```

- `rebuildScheduled` is reset to `false` inside `rebuildEventItems()`
- Prevents multiple synchronous rebuilds during rapid mutations

**Keyboard Shortcuts**:
- `Space`: Play/Pause (via `QShortcut` with `WidgetWithChildrenShortcut`)
- `Delete`/`Backspace`: Remove selected event
- `Home`: Seek to 0
- `Left`/`Right`: Nudge selected events (-1000ms / +1000ms, Shift for -100ms / +100ms)
- `Ctrl+C` / `Ctrl+V`: Copy/paste events

### 4.5 TimelineEventItem

**Files**: `src/timeline-event-item.h`, `src/timeline-event-item.cpp`

**Purpose**: Visual representation of a single event on the timeline. `QGraphicsEllipseItem` subclass with custom paint.

**Visual Properties**:
- Circular node (32x32px) with configurable color
- SVG icon overlay (type-specific or custom)
- Automatic black/white icon contrast based on background luminance
- Duration line extending right from center when `hasDuration` is true
- Stack offset: Y position shifts down by 20px per concurrent event in 50ms buckets

**Interaction**:
- `mousePressEvent`: Begins drag, emits `dragStarted()`
- `itemChange(ItemPositionChange)`: Constrains Y to stack offset, snaps X to grid if enabled
- `mouseReleaseEvent`: Emits `eventMoved(newTimeMs)` if position changed
- `mouseDoubleClickEvent`: Emits `doubleClicked()` — opens `EventEditor`
- `contextMenuEvent`: Shows menu with Color, Delete, Duplicate

### 4.6 HeadlessTimeline

**Files**: `src/headless-timeline.h`, `src/headless-timeline.cpp`

**Purpose**: Self-contained timeline runner for nested timeline triggers. No UI components.

**Lifecycle**:
1. Created by `ObsExecutor::handleTriggerTimeline()`
2. Loads event data from JSON file via `loadFromFile()`
3. Optionally reads settings (loop, endTime) from JSON object wrapper
4. `start()` begins scheduler playback
5. On `finished` signal, executor cleans up the instance

**Resource Management**: All children (model, scheduler, executor) are parented to the `HeadlessTimeline` instance and automatically deleted by Qt's parent-child mechanism. `cancelAll()` is called in the destructor to stop any active nested timelines.

---

## 5. Data Models

### 5.1 TimelineEvent

```cpp
struct TimelineEvent {
    QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    double timeMs = 0.0;
    TimelineEventType type = TimelineEventType::SceneSwitch;
    QString sourceName;     // Scene name, source name, hotkey name, or file path
    QString filterName;     // Optional: filter name for filter events
    QVariant data;          // Additional data (transition name, seek position, etc.)
    QColor color = QColor(0, 200, 255);
    QString customIconPath;
    bool hasDuration = false;
    double durationMs = 0.0;
};
```

**Field Semantics by Type**:

| Type | `sourceName` Meaning | `filterName` | `data` Usage |
|------|---------------------|-------------|-------------|
| `SceneSwitch` | Target scene name | — | Custom transition name (optional) |
| `SourceShow` / `SourceHide` | Source name | — | — |
| `MediaStart` / `MediaStop` | Media source name | — | — |
| `FilterEnable` / `FilterDisable` | Source name | Filter name | — |
| `HotkeyTrigger` | Hotkey name | — | — |
| `TriggerTimeline` | — | — | File path to timeline JSON |

### 5.2 JSON Serialization Format

**Current Format (Object Wrapper)**:

```json
{
  "events": [
    {
      "id": "550e8400-e29b-41d4-a716-446655440000",
      "time": 5000.0,
      "type": 0,
      "source": "Main Scene",
      "color": "#00c8ff",
      "hasDuration": false
    }
  ],
  "settings": {
    "loop": false,
    "endTime": 120.0,
    "snap": true,
    "snapIntervalMs": 500.0,
    "bpm": 120.0,
    "gridToBpm": false,
    "centerControls": false
  }
}
```

**Legacy Format (Bare Array)**:

```json
[
  {"time": 5000.0, "type": 0, "target": "Main Scene"}
]
```

**Migration Handling**: `TimelineModel::fromJson()` handles legacy fields:
- `"target"` is mapped to `"source"`
- `"source"::"filter"` split for filter events
- Missing `"id"` generates a new UUID
- Missing `"color"` uses `ThemeIcons::getDefaultColor()`

---

## 6. Integration Points

### 6.1 OBS Frontend API

The plugin uses the following `obs-frontend-api.h` functions:

| Function | Usage |
|---------|-------|
| `obs_frontend_add_dock_by_id()` | Registers the timeline dock in the OBS UI |
| `obs_frontend_add_save_callback()` | Binds timeline persistence to OBS save events |
| `obs_frontend_get_current_scene()` | Source visibility target lookup |
| `obs_frontend_get_scenes()` | Cross-scene source visibility fallback |
| `obs_frontend_get_current_transition()` | Transition preservation during scene switch |
| `obs_frontend_set_current_transition()` | Custom transition application |
| `obs_frontend_set_current_scene()` | Scene switching |
| `obs_frontend_preview_program_mode_active()` | Studio mode awareness |
| `obs_frontend_get_transitions()` | Transition lookup by name |
| `obs_hotkey_register_frontend()` | Play/Pause/Stop hotkey registration |
| `obs_hotkey_unregister()` | Hotkey cleanup |
| `obs_hotkey_inject_event()` | Programmatic hotkey triggering |

### 6.2 libobs API

**Scene Management**:
- `obs_scene_from_source()` — Convert scene source to scene object
- `obs_scene_find_source()` — Locate a source within a scene
- `obs_sceneitem_set_visible()` — Toggle source visibility

**Source Management**:
- `obs_get_source_by_name()` — Lookup source by name
- `obs_source_release()` — Decrement reference count
- `obs_source_get_ref()` — Increment reference count
- `obs_source_media_restart()` / `obs_source_media_stop()` — Media control
- `obs_source_get_filter_by_name()` — Filter lookup
- `obs_source_set_enabled()` — Filter enablement

### 6.3 obs-websocket Vendor API (Optional)

When built with `OBS_TIMELINE_HAS_WEBSOCKET`:

```cpp
obs_websocket_vendor vendor = obs_websocket_register_vendor("obs-timeline-plugin");
obs_websocket_vendor_emit_event(vendor, "TimelineEvent", event_data);
```

**Event Payload**:

```json
{
  "event": "started",
  "type": 0,
  "sourceName": "Main Scene",
  "timeMs": 5000.0
}
```

External consumers can subscribe to the `obs-timeline-plugin` vendor to receive real-time event notifications.

---

## 7. Deployment Architecture

### 7.1 Installation Layout

```
~/.config/obs-studio/plugins/obs-timeline-plugin/
+-- bin/64bit/
|   +-- libobs-timeline-plugin.so       # Plugin binary
+-- data/
|   +-- locale/
|   |   +-- en-US.ini                   # UI strings
```

### 7.2 Persistence Location

Timeline state is saved to:

```
~/.config/obs-studio/plugin_config/obs-timeline-plugin/timeline_events.json
```

This path is retrieved via `obs_module_get_config_path()` and is automatically scoped to the current scene collection.

### 7.3 Build Configuration

**CMake Options**:

| Option | Default | Description |
|--------|---------|-------------|
| `CMAKE_INSTALL_PREFIX` | Platform-dependent | Base installation directory |
| `OBS_PLUGIN_DESTINATION` | `"bin/64bit"` | Relative path for the plugin binary |
| `OBS_DATA_DESTINATION` | `"data"` | Relative path for data files |
| `OBS_TIMELINE_HAS_WEBSOCKET` | Auto-detected | Enable obs-websocket vendor integration |

**Build Command**:

```bash
cmake -S . -B build \
  -DCMAKE_INSTALL_PREFIX="$HOME/.config/obs-studio/plugins/obs-timeline-plugin" \
  -DOBS_PLUGIN_DESTINATION="bin/64bit" \
  -DOBS_DATA_DESTINATION="data"
cmake --build build
cmake --install build
```

---

## 8. Performance Characteristics

### 8.1 Timer Frequency

- **Tick Interval**: 16ms (~60 Hz)
- **Event Scanning**: O(n) per tick where n = number of events
- **Practical Limit**: < 1000 events at 60 Hz is negligible CPU load

### 8.2 Graphics Performance

- **Playhead Updates**: Gated by viewport-edge check to avoid `ensureVisible()` repaints on every tick
- **Rebuild Coalescing**: `Qt::QueuedConnection` batches rapid model changes into a single rebuild
- **Stacking Algorithm**: O(n log n) during rebuild due to `std::map` bucket counting

### 8.3 Memory Profile

| Component | Approximate Memory |
|-----------|-------------------|
| `TimelineModel` (1000 events) | ~200 KB |
| `QGraphicsScene` (1000 items) | ~1-2 MB |
| `TimelineScheduler` fired sets | ~50 KB |
| **Total Working Set** | < 5 MB for typical timelines |

### 8.4 Known Bottlenecks

1. **Linear ID Lookup**: `findEventIndexById()` scans the entire vector. For > 10,000 events, consider a `QHash<QString, size_t>` index.
2. **SVG Renderer Recreation**: `refreshRenderer()` deletes and reallocates `QSvgRenderer` on every color/type change. Could be cached.
3. **Full Scene Rebuild**: `rebuildEventItems()` rebuilds all items even when only one event changed. Differential updates would improve responsiveness for large timelines.

---

## 9. Security Model

### 9.1 Threat Surface

The plugin operates within the OBS process and has the same privileges as OBS itself:
- Can switch scenes, toggle sources, and trigger hotkeys
- Can read/write files in the OBS config directory
- Can emit websocket events (if enabled)

### 9.2 Input Validation

- **JSON Loading**: `TimelineSerialization::fromJsonDocument()` validates document structure but trusts event content
- **File Paths**: `TriggerTimeline` events resolve paths via `QFileInfo::canonicalFilePath()` to prevent directory traversal
- **Source Names**: Passed directly to OBS API; OBS validates source existence

### 9.3 Safe Practices

- All OBS source/scene pointers are released via `obs_source_release()`
- `obs_frontend_source_list_free()` is always paired with `obs_frontend_get_scenes()` / `obs_frontend_get_transitions()`
- No user input is passed to `system()`, `popen()`, or shell interpreters

---

## 10. Appendices

### Appendix A: Glossary

| Term | Definition |
|------|-----------|
| **Event** | A scheduled action at a specific time position on the timeline |
| **Duration Event** | An event with `hasDuration=true` that fires both a start and end action |
| **Snap** | Quantization of drag positions to a grid interval |
| **Stack Offset** | Vertical displacement applied when multiple events share the same time bucket |
| **Headless Timeline** | A timeline instance without UI, used for nested timeline triggers |
| **BPM Grid** | Snap grid calculated from beats-per-minute tempo |

### Appendix B: File Reference

| File | Lines | Purpose |
|------|-------|---------|
| `src/plugin-main.cpp` | 50 | Module entry point |
| `src/timeline-dock.cpp` | ~1267 | Main UI widget |
| `src/timeline-model.cpp` | 167 | Data model |
| `src/timeline-scheduler.cpp` | 161 | Playback scheduler |
| `src/obs-executor.cpp` | 371 | OBS action executor |
| `src/timeline-event-item.cpp` | 266 | Graphics item |
| `src/headless-timeline.cpp` | 85 | Nested timeline runner |
| `src/timeline-serialization.cpp` | 100 | JSON persistence |
| `src/event-editor.cpp` | ~350 | Event editor dialog |
| `src/event-types.h` | 34 | Shared types |
| `src/theme-helpers.h` | ~80 | SVG icons and colors |

### Appendix C: Event Type Enum

```cpp
enum class TimelineEventType {
    SceneSwitch,    // 0
    MediaStart,     // 1
    MediaStop,      // 2
    MediaSeek,      // 3 (Reserved)
    SourceShow,     // 4
    SourceHide,     // 5
    FilterEnable,   // 6
    FilterDisable,  // 7
    HotkeyTrigger,  // 8
    TriggerTimeline // 9
};
```

### Appendix D: Reading Paths

**For New Developers**:
1. Read `src/event-types.h` to understand data structures
2. Read `src/timeline-model.h` for data operations
3. Read `src/timeline-scheduler.h` for playback logic
4. Read `src/obs-executor.h` for OBS integration
5. Read `src/timeline-dock.h` for UI architecture

**For Architects**:
1. Review section 3 (Design Decisions)
2. Review section 4 (Core Components)
3. Review section 8 (Performance Characteristics)
4. Examine `src/timeline-dock.cpp` for signal flow

**For QA/Testing**:
1. Review section 4.2 (Scheduler)
2. Review section 4.3 (Executor)
3. Check `tests/` directory for existing test coverage
4. Focus on edge cases: empty timelines, rapid seek, looping

---

*End of Document*
