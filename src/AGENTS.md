# src/

## What Lives Here

Main plugin source code. Contains the dock widget (`TimelineDock`), timeline model and scheduler, OBS action executor, event graphics items, and the plugin entry point. All UI code uses Qt6 Widgets and QGraphicsView.

## Key Files

| File | Purpose |
|------|---------|
| `plugin-main.cpp` | Entry point (`obs_module_load`, `obs_module_unload`, `obs_module_post_load`) |
| `timeline-dock.cpp/.h` | Main dock widget — UI layout, event editing, playback controls |
| `timeline-model.cpp/.h` | Event data model — add/remove/update/sort events, JSON serialization |
| `timeline-scheduler.cpp/.h` | Playback scheduler — QTimer-based tick, event firing, loop/end logic |
| `obs-executor.cpp/.h` | OBS action executor — scene switch, source visibility, media, filters, hotkeys |
| `timeline-event-item.cpp/.h` | QGraphicsItem for rendering draggable timeline event bubbles |
| `headless-timeline.cpp/.h` | Standalone timeline runner for nested timeline triggers |
| `event-types.h` | Shared enums and structs (`TimelineEvent`, `TimelineEventType`) |
| `theme-helpers.h` | SVG icon helpers and default event colors |

## Deviations from Root

- Qt objects rely on parent-child ownership; prefer `deleteLater()` over direct `delete` for items that may be handling events.
- Event items are stored in a `QHash<QString, TimelineEventItem *>` keyed by event ID to support fast lookup and multiple events at the same timestamp.

## Dependencies & Side Effects

- Changing `event-types.h` affects `src/`, `tests/`, and serialization in `timeline-model.cpp`.
- `obs-executor.cpp` calls OBS frontend API (`obs_frontend_*`) and websocket API — mock these in tests.
- `timeline-dock.cpp` connects `modelChanged` to `rebuildEventItems()` via a **coalesced queued connection** to avoid deleting items during active event handling.

## Watch Out For

- **Coalesced rebuild**: `rebuildEventItems()` is queued via `QMetaObject::invokeMethod(..., Qt::QueuedConnection)`. Do not call it synchronously from item callbacks (e.g., `deleteRequested`, `eventMoved`) — let `modelChanged` handle it.
- **Scene/transition reference counting**: `obs_frontend_get_transitions()` returns sources that are released by `obs_frontend_source_list_free()`. If keeping a source beyond the list scope, call `obs_source_get_ref()` and `obs_source_release()` later.
- **Model signal cascades**: `TimelineModel` emits `modelChanged()` after every mutation. Multiple rapid mutations (nudge, paste) should not trigger multiple synchronous rebuilds.
- **Hotkey callbacks**: Hotkeys run on an arbitrary thread. Always use `QMetaObject::invokeMethod(..., Qt::QueuedConnection)` to touch Qt objects.
