# Architecture & Technical Reference

A comprehensive technical reference for the Timeline for OBS plugin. This
document is intended for developers maintaining or extending the plugin,
architects evaluating its design, and contributors onboarding to the
codebase.

---

## Executive Summary

Timeline for OBS is a dock-based OBS Studio plugin written in C++17 with
Qt6 Widgets. It introduces a visual, time-based sequencer that fires
actions at precise moments — scene switches, source visibility toggles,
media playback, filter enable/disable, hotkey triggers, and nested
timeline recursion.

**Core design principles:**

1. **Model–View separation.** `TimelineModel` holds events as plain data.
   The UI never mutates event state directly — it asks the model, which
   emits `modelChanged()` and triggers a coalesced rebuild.
2. **Scheduler ≠ Executor.** The scheduler advances the playhead and
   decides *when* events fire. The executor translates events into OBS
   API calls. Either can be replaced without touching the other.
3. **Headless reuse.** The same Model + Scheduler + Executor triple runs
   inside `HeadlessTimeline` for nested timelines, with no UI. This is
   the recursion primitive.
4. **Theme parity.** All visual elements source colors from `QPalette`
   and the `"OBSAudioMeter"` stylesheet selector, so the dock follows
   the active OBS theme without hard-coded colors.

---

## System Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│  OBS Studio Frontend                                             │
│  ┌────────────────────────────────────────────────────────────┐ │
│  │  TimelineDock  (QWidget)                                   │ │
│  │  ├── QGraphicsView + QGraphicsScene                        │ │
│  │  │   ├── TimelineEventItem  (QGraphicsEllipseItem)         │ │
│  │  │   ├── playheadItem, endMarkerItem, dragGuideItem        │ │
│  │  │   └── ruler/grid items                                  │ │
│  │  ├── Toolbars (play/stop/zoom/add/snap)                    │ │
│  │  ├── EventEditor (modal dialog)                            │ │
│  │  └── TimecodeDisplay (custom widget)                       │ │
│  │                                                            │ │
│  │  MacroStripDock  (QWidget)                                 │ │
│  │  ├── MacroButtonModel                                      │ │
│  │  └── MacroButtonWidget × N (themed buttons)                │ │
│  │                                                            │ │
│  │  Shares ObsExecutor → both docks fire events               │ │
│  └────────────────────────────────────────────────────────────┘ │
│           │                                                      │
│           ▼                                                      │
│  ┌──────────────────┐    signal     ┌─────────────────────────┐  │
│  │  TimelineModel   │ ◀───────────  │  TimelineDock / Editor  │  │
│  │  (vector<Event>) │               └─────────────────────────┘  │
│  │  emits:          │                                              │
│  │   modelChanged() │                                              │
│  └──────────────────┘                                              │
│           │                                                      │
│           ▼                                                      │
│  ┌────────────────────────┐                                      │
│  │  TimelineScheduler     │  QTimer @ ~60 FPS                   │
│  │  currentOffsetMs       │  ──▶ tick(t)                         │
│  │  eventStartFired       │  ──▶ eventStarted(Event)            │
│  │  eventEndFired         │  ──▶ eventEnded(Event)              │
│  │  endTimeMs, looping    │  ──▶ finished() / looped()          │
│  └────────────────────────┘                                      │
│           │                                                      │
│           ▼                                                      │
│  ┌────────────────────────┐                                      │
│  │  ObsExecutor           │  obs_frontend_set_current_scene    │
│  │  performAction()       │  obs_source_media_restart          │
│  │  handleSceneSwitch()   │  obs_sceneitem_set_visible          │
│  │  handleMedia*()        │  obs_source_set_enabled (filter)   │
│  │  handleSourceVis()     │  obs_hotkey_trigger_routed_callback │
│  │  handleFilterEnable()  │  obs_websocket_vendor_emit_event   │
│  │  handleHotkey()        │  ──▶ HeadlessTimeline (nested)      │
│  └────────────────────────┘                                      │
└─────────────────────────────────────────────────────────────────┘
```

`HeadlessTimeline` instantiates its own Model + Scheduler + Executor
triple on demand when a `TriggerTimeline` event fires. It cleans itself
up when playback finishes or `ObsExecutor::cancelAll()` is called.

---

## Module Map

| Module                | File                              | Role                                                                |
|-----------------------|-----------------------------------|---------------------------------------------------------------------|
| `plugin-main.cpp`     | `src/plugin-main.cpp`             | Module entry. Registers docks, save callbacks, and the websocket vendor. |
| `TimelineDock`        | `src/timeline-dock.cpp`           | Main UI: scene rendering, controls, persistence, event editor launch. |
| `MacroStripDock`      | `src/macro-strip-dock.cpp`        | Secondary dock: row of triggerable macro buttons.                   |
| `TimelineModel`       | `src/timeline-model.cpp`          | Event data store. Emits `modelChanged()` on any mutation.            |
| `TimelineScheduler`   | `src/timeline-scheduler.cpp`      | QTimer-driven playhead. Emits tick + event start/end signals.        |
| `ObsExecutor`         | `src/obs-executor.cpp`            | Maps `TimelineEvent` → `obs_*` API calls. Manages nested timelines.  |
| `TimelineEventItem`   | `src/timeline-event-item.cpp`     | `QGraphicsEllipseItem` rendering an event bubble with icon + label.  |
| `EventEditor`         | `src/event-editor.cpp`            | Modal `QDialog` for creating or editing a `TimelineEvent`.          |
| `HeadlessTimeline`    | `src/headless-timeline.cpp`       | Non-UI timeline runner for `TriggerTimeline` events.                 |
| `TimecodeDisplay`     | `src/timecode-display.cpp`        | Custom widget rendering monospace playback time. Reuses `"OBSAudioMeter"` stylesheet object name. |
| `MacroButtonModel`    | `src/macro-button-model.cpp`      | CRUD over `MacroButton` records.                                     |
| `MacroButtonWidget`   | `src/macro-button-widget.cpp`     | Themed push-button for a single macro.                               |
| `MacroSerialization`  | `src/macro-serialization.cpp`      | Persist macro strip to/from JSON.                                    |
| `TimelineSerialization` | `src/timeline-serialization.cpp`| Persist timeline events + dock settings.                             |
| `theme-helpers.h`     | `src/theme-helpers.h`             | IconaMoon Bold SVG helpers, dynamic color extraction.                |
| `event-types.h`       | `src/event-types.h`               | `TimelineEvent` struct and `TimelineEventType` enum.                 |

---

## Data Model

### `TimelineEvent`

`src/event-types.h:21-35` — Plain data struct. Every field has a
sensible default so default-construction yields a valid event:

```cpp
struct TimelineEvent {
    QString id;          // UUID, auto-generated
    double timeMs;       // When the event fires
    TimelineEventType type;
    QString sourceName;  // Scene, source, or hotkey name
    QString filterName;  // For filter-targeted events
    QVariant data;       // Type-specific payload (e.g. transition name)
    QColor color;        // Bubble color
    QString customIconPath;
    QString label;       // Optional display text
    bool showLabel;      // Whether to render the label on the bubble
    bool hasDuration;
    double durationMs;
};
```

### Event Types

`src/event-types.h:8-19` — 9 variants across 6 categories. `MediaSeek`
is reserved/deprecated and intentionally not exposed in the UI.

| Category        | Variants                                          |
|-----------------|---------------------------------------------------|
| Scene Switch    | `SceneSwitch`                                     |
| Source Visibility | `SourceShow`, `SourceHide`                     |
| Media Control   | `MediaStart`, `MediaStop` (and reserved `MediaSeek`) |
| Filter Control  | `FilterEnable`, `FilterDisable`                   |
| Hotkey Trigger  | `HotkeyTrigger`                                   |
| Nested Timeline | `TriggerTimeline`                                 |

### `TimelineModel`

`src/timeline-model.cpp:5-181` — Owns a `std::vector<TimelineEvent>`
kept sorted by `timeMs`. Every mutation re-sorts and emits
`modelChanged()`. All lookups are by UUID (`QString id`).

Key methods: `addEvent`, `removeEventById`, `updateEventTimeById`,
`replaceEventById`, `findEventById`, `toJson`, `fromJson`.

---

## Playback Lifecycle

### Tick loop

`src/timeline-scheduler.cpp:153-201` — A `QTimer` fires every 16 ms
(~60 FPS). On each tick:

1. Compute `currentTime = currentOffsetMs + elapsed`.
2. If `endTimeMs > 0 && currentTime >= endTimeMs`:
   - If looping: reset state, fire `looped()`.
   - Else: call `pause()`, fire `finished()`.
3. Emit `tick(currentTime)`.
4. For each event not yet in `eventStartFired`: if `evt.timeMs <= currentTime`, emit `eventStarted` and add to set.
5. For each event with duration: if `endMs <= currentTime` and not yet in `eventEndFired`, emit `eventEnded` and add to set.

The `eventStartFired` / `eventEndFired` sets prevent double-firing
across tick boundaries. They are cleared on `seek()` and
`modelChanged()` (with hysteresis to preserve "already fired" status
when an event's time is in the past).

### Event execution

`src/obs-executor.cpp:75-127` — `performAction()` dispatches by
`evt.type`:

- **`SourceShow`/`SourceHide`** → `handleSourceVisibility(evt, visible)`.
  When `hasDuration`, start = visible, end = hidden. Without duration,
  the type itself encodes the visibility.
- **`MediaStart`/`MediaStop`** → `handleMediaStart/Stop` calls
  `obs_source_media_restart/stop`.
- **`FilterEnable`/`FilterDisable`** → `handleFilterEnable(evt, enable)`.
  Same duration semantics as visibility.
- **`SceneSwitch`** → `handleSceneSwitch(evt)`. Optional transition name
  stored in `evt.data` is applied for one switch then restored.
- **`HotkeyTrigger`** → `handleHotkey(evt)` enumerates registered
  hotkeys and triggers the named one. The enumeration collects an ID
  first, triggers after — to avoid mutating the hotkey table during
  iteration.
- **`TriggerTimeline`** → `handleTriggerTimeline(evt)`. Spins up a
  `HeadlessTimeline` with the file path stored in `evt.data` (falls
  back to `evt.sourceName` for backward compatibility). Recursion is
  prevented by comparing canonical file paths of active nested
  timelines.

### Cancel

`src/obs-executor.cpp:25-33` — `cancelAll()` iterates active nested
timelines, calls `stop()` and `deleteLater()`. Invoked on
`scheduler::pause()` and on OBS exit.

---

## UI Layer

### Dock wrapping

`src/plugin-main.cpp:62` — The plugin is a Qt widget, not a
`QDockWidget`. OBS wraps the widget via
`obs_frontend_add_dock_by_id("timeline_dock", "Timeline", g_timelineDock)`.
The dock title is shown in OBS's **View > Docks** menu. The same
pattern is used for the macro strip
(`obs_frontend_add_dock_by_id("timeline_macro_strip", "Macro Strip", ...)`).

### Rendering pipeline

`src/timeline-dock.cpp:931-965` — `updateLayout()` runs:

1. Set `sceneRect` (with 20 px left padding so a t=0 marker is
   grabbable from the right half).
2. Re-paint every event item with current zoom (`setPixelsPerSecond`)
   and theme colors. Drag signals are suppressed during this pass
   (`setSuppressDragSignals(true/false)`).
3. Redraw the ruler and grid (cached on `(pixelsPerSecond, sceneWidth)`
   to avoid redundant rebuilds).
4. Reposition the end marker (created once, repositioned thereafter
   — see [Design Decisions](#design-decisions)).
5. Update the playhead.

### Coalesced rebuild

`src/timeline-dock.cpp:69-77` — `modelChanged` triggers
`rebuildEventItems` via a queued connection, but only if no rebuild is
already pending:

```cpp
if (!rebuildScheduled) {
    rebuildScheduled = true;
    QMetaObject::invokeMethod(this, &TimelineDock::rebuildEventItems, Qt::QueuedConnection);
}
```

This collapses bursts of model mutations (e.g. paste of N events) into
a single rebuild. During an in-progress drag the rebuild is deferred
and re-scheduled on mouse release.

### Event item rendering

`src/timeline-event-item.cpp:227-343` — A `TimelineEventItem` is a
`QGraphicsEllipseItem` with these paint layers (back to front):

1. **Duration line** (if `hasDuration`): a thick rounded line from the
   bubble center to `+ durationMs`, capped with a vertical tick at the
   end.
2. **Hover glow** (if `isHovered`): a larger, semi-transparent ellipse
   in a lighter shade.
3. **Selection ring** (if selected): two concentric circles — a
   background-colored outer ring for contrast and a theme-highlight
   inner ring.
4. **Main circle**: filled with `eventColor`, stroked with
   `borderColor`.
5. **Icon**: rendered via `QSvgRenderer` (the event-type icon, baked
   black or white based on background luminance, or a user-supplied
   custom SVG).
6. **Label pill** (if `showLabel && !eventLabel.isEmpty()`): a
   rounded rect above the bubble, painted with theme-derived
   `labelTextColor` / `labelBgColor` for legibility on any background.

### Theme integration

`src/theme-helpers.h` — The plugin reads all colors from `QPalette`
and `obs_frontend_get_theme_path()` derived stylesheet selectors.
Notable tricks:

- `TimecodeDisplay` sets its `objectName` to `"OBSAudioMeter"` so OBS
  theme QSS rules (originally written for audio meters) style the
  monospace playback clock.
- `playheadColor` is `QPalette::Link` (theme accent).
- `endMarkerItem` is also `QPalette::Link` with 180/255 alpha.
- Toolbar icon buttons bake their SVG path with `ThemeIcons::wrapIconMoon`
  using `iconColorFor(button, onHighlight)` which picks text vs.
  highlighted-text based on the button's current state.

Theme change is observed via `OBS_FRONTEND_EVENT_THEME_CHANGED`,
re-applies colors to all items, and refreshes icons.

---

## Data Flow

### Event creation

```
User double-clicks event bubble (or presses Add Event)
  → TimelineDock::onEventDoubleClicked / onAddEventClicked
  → EventEditor modal opens
  → user picks type, target, color, label, duration
  → TimelineDock creates TimelineEvent with QUuid
  → model->addEvent(evt)
  → model sorts, emits modelChanged()
  → TimelineDock coalesces and calls rebuildEventItems()
  → new TimelineEventItem is added to scene
  → debounceSaveTimer fires (250 ms)
  → TimelineSerialization::saveToFile writes timeline.json
```

### Playback tick

```
User presses Space (or playButton)
  → TimelineDock::onPlayClicked
  → scheduler->start() — captures startTime, starts QTimer
  → 16 ms later: scheduler::onTimerTick
  → emits tick(t)  → TimelineDock::onTick updates playhead + TimecodeDisplay
  → for events at t: emits eventStarted(evt) → ObsExecutor::executeEventStart
  → ObsExecutor calls obs_frontend_set_current_scene / obs_source_* etc.
  → optionally emits TimelineEvent websocket vendor event
```

### Nested timeline recursion

```
TimelineEvent with type=TriggerTimeline, data=path/to/child.json
  → ObsExecutor::handleTriggerTimeline
  → creates new HeadlessTimeline(parent=this)
  → HeadlessTimeline::loadFromFile
  → connects HeadlessTimeline::finished → cleanup
  → HeadlessTimeline::start
  → its own TimelineScheduler runs the nested events
  → when finished, deletes itself
```

Recursion is prevented by checking the canonical path of each active
nested timeline against the new request.

### Persistence

`src/timeline-serialization.cpp` and `src/macro-serialization.cpp`.

Timeline JSON schema (root object):

```json
{
  "events": [ { "id": "...", "time": 1500, "type": 0, "source": "Scene 1", ... } ],
  "settings": {
    "loop": false, "endTime": 0, "snap": false, "snapIntervalMs": 500,
    "bpm": 120, "gridToBpm": false, "noteLengthBeats": 1,
    "centerControls": false, "alternatingBg": false, "compactMode": false,
    "showEndTimeOnDock": true, "currentTimeMs": 0, "zoomPixelsPerSecond": 99
  }
}
```

Legacy bare-array JSON (events only) is still accepted and round-trips
as an empty settings object.

Per-event schema — optional fields are omitted when at their default
to keep files small:

```json
{
  "id": "uuid", "time": 1500, "type": 0, "source": "Scene 1",
  "color": "#ff7700",
  "label": "Optional", "showLabel": true,
  "hasDuration": true, "duration": 5000,
  "data": "Fade"
}
```

---

## Threading Model

- The **UI thread** owns all `QObject`s. Signals/slots are direct or
  `Qt::QueuedConnection` as appropriate.
- **Hotkey callbacks** (registered with `obs_hotkey_register_frontend`)
  fire on an OBS-owned thread. The plugin wraps every hotkey
  handler with `QMetaObject::invokeMethod(dock, "onPlayClicked",
  Qt::QueuedConnection)` to bounce back to the UI thread before
  touching any Qt object.
- **The `QTimer` for playback** runs on the UI thread. Each tick
  reads the model and emits signals to the executor on the same
  thread, so no locking is needed for event data.
- **WebSocket vendor** events are emitted from whatever thread the
  executor is invoked on (UI thread in practice). No threading
  concerns.

The OBS graphics thread is never touched by this plugin — we render
in a `QGraphicsView` widget, not on the OBS output canvas.

---

## Design Decisions

### Why QGraphicsView instead of a custom widget?

`QGraphicsView` gives us free hit-testing, scene transforms (zoom),
and event-driven item interaction. Custom drag, hover, and selection
on per-event bubbles would be many hundreds of lines of bookkeeping.
The cost is that we must be careful with item lifecycle and
rebuild timing.

### Coalesced modelChanged → rebuild

If we rebuild on every single `modelChanged`, dragging a single event
emits a `modelChanged` per pixel of movement (from `setTime` re-sort
on every drag tick). The coalescing pattern (`rebuildScheduled` +
queued `invokeMethod`) collapses bursts of changes and skips the
rebuild entirely while a drag is active. The rebuild is re-scheduled
on mouse release. This eliminates flicker and dramatically reduces
GPU work.

### `suppressDragSignals` around programmatic moves

When the dock sets an event's position during a rebuild
(`setTimeMs(evt.timeMs)`), Qt's `ItemPositionChange` callback fires
`dragMoved()` which would cause a feedback loop. The item has a
`suppressDragSignals` flag set around any property setter that calls
`setPos` internally.

### Show Label is opt-in, not opt-out

By default events render as a clean icon-only bubble. Labels appear
above the bubble only when the user checks "Show Label" in the event
editor. This keeps the timeline visually quiet by default and
prevents label-pill collision when many events stack at the same
time.

### `deleteLater` is the default, but `QSvgRenderer` is `delete`-d

`src/AGENTS.md` prefers `deleteLater()` because it is safe for
objects that may be mid-event-handler. `QSvgRenderer` inside
`TimelineEventItem` is the documented exception: it owns no
deferred events, the item owns it exclusively, and `deleteLater()`
risks a use-after-free when items are rapidly rebuilt (e.g. file
load).

### Headless reuse of the Model/Scheduler/Executor triple

When a `TriggerTimeline` event fires, the executor instantiates
`HeadlessTimeline` which contains its own `Model`, `Scheduler`, and
`Executor`. This is the smallest possible recursion primitive — we
don't have to special-case nested timeline event firing; the same
loop just runs in another thread of execution. The cost is a
trivially small extra allocation per nest level.

### `HeadlessTimeline` validates paths to prevent recursion

A cycle (A triggers B triggers A) would deadlock the playhead. We
prevent this by comparing the canonical file path of each
in-progress nested timeline against the new request.

---

## Performance

- **Render caching**: Ruler and grid rebuilds are gated on
  `(pixelsPerSecond, sceneWidth)`. Zooming typically keeps these
  values stable so neither redraws.
- **End-marker caching**: `endMarkerItem` and `postEndOverlay` are
  created once and repositioned, not recreated, on every zoom/resize.
- **Pixmap caching**: `playheadHandlePixmap` and
  `endMarkerHandlePixmap` are cached by color, so theme changes
  invalidate the cache but normal usage reuses the same pixmap.
- **Model sort**: `std::sort` over events on every mutation is O(n
  log n). Event counts are small (tens to low hundreds typical), so
  this is comfortably interactive.

---

## Testing

`tests/AGENTS.md` is the source of truth. Briefly:

- `tests/main_test.cpp` defines `TestTimeline` with the current suite
  of event-execution tests (`testSceneSwitch`, `testMediaStart`, etc.).
- `tests/mock_frontend.cpp` provides no-op stubs for `obs_frontend_*`
  so tests can run without OBS.
- `tests/mock_obs_websocket.cpp` stubs `obs_websocket_*`.
- `QT_QPA_PLATFORM=offscreen` is set in `main()` so tests run
  headless.

Run with:

```bash
cd build && ./tests/run_tests
cd build && ./tests/run_tests testSceneSwitch
```

---

## Reading Paths

| If you are a …                       | Start with …                                                       |
|--------------------------------------|--------------------------------------------------------------------|
| New contributor                      | `README.md`, then `AGENTS.md`, then this document.                 |
| Plugin developer adding a feature    | `docs/ARCHITECTURE.md` (this), then the source file you intend to change. |
| Someone debugging a render glitch    | `TimelineEventItem::paint` and `TimelineDock::updateLayout`.       |
| Someone debugging playback           | `TimelineScheduler::onTimerTick` and `ObsExecutor::performAction`. |
| Someone writing tests                | `tests/AGENTS.md`, then a small test in `main_test.cpp` as a template. |
| Maintainer preparing a release       | `docs/RELEASE_CHECKLIST.md`.                                        |

---

## Glossary

- **Dock** — A Qt widget registered with OBS via
  `obs_frontend_add_dock_by_id`, visible under **View > Docks**.
- **Tick** — One iteration of the scheduler's QTimer (16 ms).
- **Event bubble** — The on-screen `QGraphicsEllipseItem` for a
  `TimelineEvent`.
- **Playhead** — The vertical line that indicates the current
  playback time.
- **End marker** — The dashed vertical line that marks
  `endTimeMs`; playback stops or loops here.
- **Loop** — When playback reaches the end marker, automatically
  seek to 0 and continue.
- **Snap** — Snapping event positions to a configurable grid
  (time-based or BPM-based).
- **Coalesced rebuild** — Multiple `modelChanged` signals collapse
  into a single `rebuildEventItems` invocation.
- **Headless timeline** — A timeline runner with no UI, used for
  nested timeline execution.
- **TriggerTimeline event** — An event whose action is to start
  another timeline file.
