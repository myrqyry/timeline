# Architecture & Technical Reference

This document provides an overview of the Timeline plugin's architecture, design decisions, and data flows.

## UI Layer (TimelineDock)

The `TimelineDock` extends `QWidget` and is registered as an OBS dock via `obs_frontend_add_dock_by_id()` in `plugin-main.cpp`.
- **QGraphicsView / QGraphicsScene**: The main timeline area where `TimelineEventItem`s are drawn.
- **Custom Widgets**: `TimecodeDisplay` is used for the playback clock. It sets its object name to `"OBSAudioMeter"` so OBS theme stylesheets style it with audio meter colors.
- **Toolbar**: All primary actions (Play, Stop, Settings, Add Event) are normalized to a 32px height (28px in Compact Mode). Icons are dynamically baked from SVG path data using the `IconaMoon Bold` set and tinted based on the current OBS palette and selection state.

## Data Layer (TimelineModel & Serialization)

- **TimelineModel**: The central store for all `TimelineEvent` structs. It manages sorting and signals (`modelChanged`) whenever events are added, removed, or updated.
- **Serialization**: `TimelineSerialization` handles importing and exporting the model and dock settings to `timeline.json` using `QJsonObject`.

## Playback & Execution (TimelineScheduler & ObsExecutor)

- **TimelineScheduler**: Uses a `QTimer` to advance `currentTimeMs`. It emits `eventStarted` and `eventEnded` signals based on precise time comparisons.
- **ObsExecutor**: Listens to the scheduler and translates events into actual OBS Studio API calls (e.g., `obs_frontend_set_current_scene`, `obs_source_media_play_pause`).

## Theme Integration

The plugin strictly adheres to OBS Theme Development guidelines:
- Listens for `OBS_FRONTEND_EVENT_THEME_CHANGED`.
- Uses Qt palettes (`QPalette::Link`, `QPalette::Window`) and dynamically queries `audioMixer` meters to extract specific theme colors (`greenColor`) for playback status indication.
