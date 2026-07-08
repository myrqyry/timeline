# Timeline for OBS

A timeline-based sequencer plugin for OBS Studio. Schedule and automate
your stream using a visual, time-based interface.

![Timeline dock screenshot](Screenshot.png)

> [!IMPORTANT]
> Timeline for OBS is an early Linux preview.
> It currently requires building from source and has not yet been broadly
> tested across OBS versions or distributions. Back up important scene
> collections before using it in a production stream.

---

## Table of Contents

- [Features](#features)
- [Screenshots](#screenshots)
- [Installation](#installation)
- [Usage](#usage)
- [Architecture](#architecture)
- [Development](#development)
- [Roadmap](#roadmap)
- [Contributing](#contributing)
- [License](#license)

---

## Features

### Visual Timeline

- **Dedicated OBS dock** — appears under **View > Docks > Timeline**.
- **Circular event bubbles** rendered with high-clarity **SVG icons**
  for: Scene Switch, Source Show/Hide, Media Start/Stop, Filter
  Enable/Disable, Hotkey Trigger, and Nested Timeline Trigger.
- **Smart contrast** — icons automatically switch between black and
  white for perfect legibility on any bubble color.
- **Stack layout** — events placed at the same time automatically
  stack vertically instead of overlapping.
- **Configurable zoom** — hold `Ctrl` + scroll to zoom from 5 seconds
  to 10 minutes view, in stepped levels.
- **BPM-aware grid** — snap events to beat subdivisions when working
  on music-synced content.

### Automation

- **9 event types across 6 categories** — Scene Switch, Source
  Visibility, Media Control, Filter Control, Hotkey Trigger, Nested
  Timeline.
- **Duration events** — show a source for 5 s, hide it after 10 s,
  etc. The end of a duration automatically reverses the start action.
- **Looping** — set a timeline end time and loop indefinitely for
  background animations.
- **Nested timelines** — trigger another timeline file as an event.
  Cycles are detected and prevented.
- **Optional transition** — pick a custom OBS transition for each
  scene switch, applied for that switch and then restored.
- **WebSocket vendor events** — broadcasts `TimelineEvent` and
  `PlaybackStatus` events to OBS WebSocket 5.x consumers when the
  websocket header is available at build time.

### Editing

- **Double-click to edit** any event.
- **Color and icon customization** per event, with a context menu for
  quick color changes.
- **Show Label** opt-in checkbox — display a text label on a bubble
  (off by default, keeps the timeline visually clean).
- **Import/Export** your full timeline to JSON for backup or sharing.
- **Drag & Drop** with optional grid snapping.
- **Keyboard shortcuts**:
  - `Space` — Play / Pause
  - `Delete` — Remove selected event
  - `Home` — Jump to start
  - `←/→` — Nudge selected events by 1 s
  - `Shift+←/→` — Fine nudge by 100 ms
  - `Ctrl+C` / `Ctrl+V` — Copy/Paste events

### Macro Strip (Secondary Dock)

A separate dock under **View > Docks > Macro Strip** holds reusable
triggerable buttons. Click a button to fire its associated OBS action
immediately. Macros are persisted alongside timeline settings and share
the same `ObsExecutor` as the timeline so behavior is consistent.

---

## Screenshots

![Timeline dock](Screenshot.png)

The dock shows the playhead (solid red line), an end marker
(dashed blue line at the right), one event bubble, and a ruler
with time labels. The toolbar along the top controls playback and
editing.

---

## Installation

### Linux (Source Build)

**Prerequisites:**

- OBS Studio built from source (with development headers)
- CMake 3.16 or newer
- Qt6 development packages (`qt6-base-dev` on Debian/Ubuntu)
- A C++17 toolchain (`build-essential` on Debian/Ubuntu)
- (Optional) `obs-websocket` headers for WebSocket vendor events

**Build & install:**

```bash
cmake -S . -B build
cmake --build build
cmake --install build
```

This installs the plugin to
`~/.config/obs-studio/plugins/timeline/`. Open OBS and look for
**Timeline** under **View > Docks**.

### Other platforms

> Windows and macOS source builds should work but are not part of the
> CI matrix yet. The plugin does not yet have packaged releases for
> any platform. See [Roadmap](#roadmap) for status.

---

## Usage

1. **Open the dock**: **View** > **Docks** > **Timeline**.
2. **Add an event**: click **Add Event** to place a marker at the
   playhead, or right-click empty timeline space for "Add Event Here".
3. **Configure the event**: double-click the marker, pick a type and
   target, optionally check **Show Label** and enter a label text.
4. **Refine**: drag the marker to position, right-click for color or
   delete. Use the **Snap** button in the toolbar to enable grid
   snapping.
5. **Play**: click Play, or press `Space`.

Persisted settings (zoom, snap, end time, loop) are saved to
`~/.config/obs-studio/plugins/timeline/timeline.json` on each change
(debounced 250 ms).

---

## Architecture

For a comprehensive technical reference covering component design,
data flow, threading model, design decisions, and reading paths, see:

**[`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md)**

### Quick Component Overview

| Component           | Source                          | Responsibility                            |
|---------------------|---------------------------------|-------------------------------------------|
| `TimelineModel`     | `src/timeline-model.cpp`         | Event data store, sort, JSON serialization |
| `TimelineScheduler` | `src/timeline-scheduler.cpp`     | QTimer playback loop, event firing        |
| `ObsExecutor`       | `src/obs-executor.cpp`           | Maps events to OBS API calls              |
| `TimelineDock`      | `src/timeline-dock.cpp`          | Main UI — scene, controls, editor launch  |
| `TimelineEventItem` | `src/timeline-event-item.cpp`    | Draggable graphics bubble                 |
| `MacroStripDock`    | `src/macro-strip-dock.cpp`       | Secondary dock of triggerable buttons     |
| `HeadlessTimeline`  | `src/headless-timeline.cpp`      | Non-UI timeline runner for nesting        |

---

## Development

### Build

```bash
cmake -S . -B build
cmake --build build
cmake --install build
```

### Test

The test suite uses Qt Test with mocked OBS and WebSocket APIs so it
runs headless.

```bash
cd build && ./tests/run_tests
# Run a single test function
cd build && ./tests/run_tests testSceneSwitch
```

### Code Style

- C++17, 4-space indent, max 100 chars (prefer 80).
- `PascalCase` for classes, `camelCase` for functions/variables,
  `mCamelCase` for private members, `snake_case` for files.
- All OBS logging via `blog(LOG_LEVEL, ...)` — never `printf` or
  `std::cout`.
- Always use OBS allocators (`bmalloc`, `bzalloc`, `bfree`) — mixing
  with `std::malloc` crashes immediately.
- Hotkey callbacks run on OBS threads; wrap with
  `QMetaObject::invokeMethod(..., Qt::QueuedConnection)`.

See the root `AGENTS.md` for the full developer handbook and the
folder-specific `AGENTS.md` files (`src/`, `tests/`, `data/`) for
routing and component-specific rules.

### Versioning

Never edit version strings in source. Edit `project(timeline VERSION ...)`
in `CMakeLists.txt` line 3.

---

## Roadmap

### In progress

- **Undo/Redo** — History stack for reverting timeline changes.
- **Multi-selection** — Box selection and shift-click for batch
  operations.

### Planned

- **Unified Visibility events** — merge Source Show/Hide and Filter
  Enable/Disable into unified event types with an internal toggle
  state.
- **Windows + macOS builds** — packaged installers and CI matrices.
- **HTTP action events** — fire HTTP requests from the timeline.

### Released (v0.1.0)

- Visual dock with 9 event types, duration, loop, snap, zoom, BPM
  grid, copy/paste, JSON import/export, OBS WebSocket vendor events,
  macro strip dock, theme support.

---

## Contributing

Contributions are welcome. Please:

1. Read `AGENTS.md` (root) and the folder-specific `AGENTS.md` for
   the area you intend to change (`src/`, `tests/`, or `data/`).
2. Add or update tests when changing runtime behavior.
3. Keep commits scoped — one feature or fix per commit.
4. Avoid `git add .`; stage only the files you changed.
5. If your change touches a documented feature, update the README or
   `docs/ARCHITECTURE.md` in the same commit.

---

## License

GPL-2.0-or-later
