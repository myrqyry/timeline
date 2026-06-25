# Macro Strip Dock — Design

**Date:** 2026-06-21
**Status:** Approved (Approach A)
**Scope:** v1 — buttons only, single row, separate OBS dock.

## Purpose

Add a second OBS dock to the existing OBS Timeline Plugin. The new dock is a thin, single-row strip of buttons that fire any trigger type the timeline dock already supports. It is a general-purpose live control surface intended to grow alongside the timeline dock: buttons in v1; knobs/sliders in a later version once the timeline supports parameter events.

## Decisions captured in brainstorming

| Question | Decision |
|---|---|
| Primary purpose | General purpose, live + macro, grows with the timeline dock |
| Strip height | Single tall row, ~48–56 px |
| Control kinds in v1 | Buttons only |
| Trigger sources in v1 | Mirror timeline trigger types |
| Editing model | Reuse `EventEditor` dialog for add/edit |
| Reorder | Context menu Move Left / Move Right |
| Button visual | Icon + label (timeline-style, IconaMoon) |
| State behavior | Visual pressed state reflecting OBS state |
| Extra actions | Right-click Edit / Move Left / Move Right / Delete |
| Overflow | Single row, horizontal scroll |
| Persistence | Separate model + JSON file |
| Plugin identity | Separate OBS dock id |

## Architecture

The plugin already registers a single OBS dock from `obs_module_load`. We add a second dock:

- New dock id: `timeline_macro_strip`.
- New module-level pointer `g_macro_strip_dock : QPointer<MacroStripDock>` in `plugin-main.cpp`, mirroring the existing timeline pointer pattern.
- New save/load callbacks `macroStripSaveCallback` / `macroStripLoadCallback` for OBS persistence.
- New persistence file: `~/.config/obs-studio/plugins/timeline/data/macro_buttons.json` (parallel to `timeline.json`).
- Module entry registers the dock via `obs_frontend_add_dock_by_id`. Teardown calls `obs_frontend_remove_dock("timeline_macro_strip")` then `g_macro_strip_dock->deleteLater()`.

### Why Approach A (separate dock + separate model)

- Independent move/resize of the strip relative to the timeline.
- Clean separation: instant actions do not pollute the timeline model.
- Future parameter-event / knob features have room to grow without retrofitting the timeline.
- Mirrors the existing `TimelineDock` shape, so reviewers already know the ownership patterns.

## Components

### `MacroButton` (struct)

```cpp
struct MacroButton {
    QString id;            // UUID
    QString label;
    TimelineEventType type;
    QString sourceName;
    QString filterName;
    QString data;
    QColor color;
};
```

Lives in `src/macro-button.h`.

### `MacroButtonModel : QObject`

- Owns `QList<MacroButton>`.
- API: `addButton(MacroButton)`, `removeButton(QString id)`, `moveButton(QString id, int newIndex)`, `replaceButtonById(QString id, MacroButton)`, `findById(QString id)`, `buttons() const`.
- Emits a single `modelChanged()` signal.
- `moveButton` clamps `newIndex` to `[0, size-1]`.
- Deduplicates IDs on load (later occurrences get fresh UUIDs).

Lives in `src/macro-button-model.{h,cpp}`.

### `MacroStripDock : QWidget`

- Owns `MacroButtonModel` and the plugin’s shared `ObsExecutor`.
- Header: small `+ Add` button (also reachable via right-click on empty strip body).
- Body: `QScrollArea` containing a single `QHBoxLayout` of `MacroButtonWidget`s.
- Right-click on a button shows context menu: Edit / Move Left / Move Right / Delete.
- Add/Edit action reuses `EventEditor` with `timeMs = currentTime` as the disabled time anchor (same convention as new timeline events; the editor time field stays disabled).
- Listens on `OBS_FRONTEND_EVENT_THEME_CHANGED` to refresh button icons/colors.
- Reuses `ThemeIcons::iconColorFor`, `IconaMoon` glyphs, and `computePlayheadColor`-style theme refresh pattern from `TimelineDock`.

Lives in `src/macro-strip-dock.{h,cpp}`.

### `MacroButtonWidget : QPushButton`

- Subclass of `QPushButton` styled like a timeline event bubble (icon + label, theme-aware colors, pressed-state reflecting OBS state).
- Holds a `MacroButton` and emits `fireRequested(MacroButton)` on click.
- Provides a `setPressedAppearance(bool)` slot the dock uses to reflect OBS source/filter state when applicable.

Lives in `src/macro-button-widget.{h,cpp}`.

## Data flow

### Add button

1. User clicks `+` (or right-clicks empty strip → Add).
2. Dock opens `EventEditor(timeMs = currentTime, parent = dock)`.
3. On Accept, dock builds a `MacroButton` from the dialog:
   - `id = QUuid::createUuid().toString(WithoutBraces)`
   - `label = event.label` (fallback to `sourceName` or type name)
   - `type`, `sourceName`, `filterName`, `data`, `color` from the dialog
4. Dock calls `model->addButton(button)`.
5. Model emits `modelChanged()`. Dock handler rebuilds button widgets.

### Fire button

1. User clicks a button widget.
2. Widget emits `fireRequested(macroButton)`.
3. Dock constructs a `TimelineEvent` from the `MacroButton` and calls `ObsExecutor::executeEventStart(evt)`.
4. For toggle-shaped types (SourceShow/SourceHide, FilterEnable/FilterDisable), the dock reads back state via the existing frontend/source APIs and calls `setPressedAppearance(true/false)` on the widget.
5. For momentary types (HotkeyTrigger, MediaStart/MediaStop, SceneSwitch, TriggerTimeline), the button just fires and stays unpressed.

### Reorder / edit / delete

- Move Left / Move Right → `model->moveButton(id, newIndex)` → `modelChanged()` → rebuild.
- Edit → same flow as Add, but pre-populating the dialog from the existing button.
- Delete → `model->removeButton(id)` → `modelChanged()` → rebuild.

### Persistence

- `modelChanged()` starts a debounce save timer (reuse the pattern from `TimelineDock::debounceSaveTimer`).
- `OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGING` triggers an immediate save.
- `OBS_FRONTEND_EVENT_EXIT` triggers an immediate save.
- On construction, dock loads `macro_buttons.json`. If absent, dock creates an empty model and writes a default file.
- Malformed JSON: log the error, rename the bad file to `macro_buttons.json.broken-<unix-seconds>`, start with an empty model.
- Duplicate IDs in loaded data: dedupe, keep first occurrence.

### Theme

- Use `ThemeIcons::iconColorFor` and the same IconaMoon glyph set as the timeline dock.
- On `OBS_FRONTEND_EVENT_THEME_CHANGED`, dock refreshes button icons/colors via the same pattern as `TimelineDock::applyThemeColorsToItems`.

## Error handling

| Failure | Behavior |
|---|---|
| Target source/scene missing at fire time | `ObsExecutor` already no-ops; dock logs `blog(LOG_WARNING, ...)` with the button label. No popup. |
| Malformed JSON on load | Log error, rename bad file with timestamp suffix, start with empty model. |
| Duplicate IDs on load | Dedupe; later occurrences get fresh UUIDs. |
| `ObsExecutor` cannot run (no OBS frontend) | Fire still goes through `ObsExecutor` which is OBS-frontend-free; tests build the dock without frontend mocks for model-only paths. |
| Edit dialog canceled | Discard; no model change. |
| Reorder out of bounds | `moveButton` clamps `newIndex` to `[0, size-1]`. |
| Dock construction before OBS frontend ready | Dock defers `load()` to `OBS_FRONTEND_EVENT_FINISHED_LOADING`, mirroring the timeline dock. |

## Testing

Add `tests/macro_strip_test.cpp`, link via existing `tests/CMakeLists.txt`:

1. `testMacroButtonModelAddRemoveMove` — add N buttons, reorder, remove, verify list state and `modelChanged` emission counts.
2. `testMacroButtonModelDeduplicatesIdsOnLoad` — load JSON with duplicate IDs, expect dedup.
3. `testMacroButtonJsonRoundTrip` — save current model to JSON, load into a new model, expect deep-equal field-by-field.
4. `testMacroStripDockFireButtonUsesObsExecutor` — register a dummy `obs_source_t*` named like the button’s `sourceName`, fire the button, assert `g_current_scene`/recorded executor state changed.
5. `testMacroStripDockContextMenuActions` — build the dock in headless mode, invoke move/edit/delete via direct API, verify model position and contents.

Existing `tests/mock_frontend.cpp` is reused. Verification gate per `verification-before-completion`: full build + `./tests/run_tests -v1` must show 0 failures before commit.

## Out of scope (later versions)

- Knobs and sliders (depend on parameter events landing in the timeline).
- Multi-page strips.
- User-defined macros (sequences of triggers).
- obs-websocket request actions.
- Drag-to-reorder.
- Button-specific icons (per-button custom glyph).