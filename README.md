# Timeline for OBS

A timeline-based sequencer plugin for OBS Studio. Schedule and automate your stream with precision using a visual timeline interface.

![Timeline Dock](Screenshot.png)

## Key Features

### 🎨 Visual Timeline
- **Precise Control**: Dedicated dock in OBS for frame-perfect scheduling.
- **Visual Markers**: Events appear as **circular nodes** with high-clarity **SVG icons**:
  - 🎬 Scene Switch
  - 👁 / 🚫 Source Show / Hide
  - ▶ / ⏹ Media Start / Stop
  - ✓ / ✗ Filter Enable / Disable
  - ⌨ Hotkey Trigger
  - 📎 Nested Timeline Trigger
- **Smart Contrast**: Icons automatically switch between Black and White for perfect visibility on any background color.
- **Smart Layout**: Events placed at the same time automatically **stack vertically**.
- **Zoomable**: Hold `Ctrl` + Scroll to zoom from 5 seconds to 10 minutes view.

### ⚡ Powerful Automation
- **10 Event Types**:
  1. **Scene Switch**: Cut to a specific scene (with optional custom transition).
  2. **Source Visibility**: Show/Hide specific sources in any scene.
  3. **Media Control**: Start/Stop media sources.
  4. **Filter Control**: Enable/Disable filters on sources.
  5. **Hotkey Trigger**: Trigger any registered OBS hotkey.
  6. **Nested Timeline**: Trigger another timeline file for complex sequences.
- **Duration Events**: Events can have a duration, automatically reversing their action at the end (e.g., show source for 5s, then hide).
- **Looping**: Set a timeline end time and loop indefinitely for complex background animations.
- **Snap to Grid**: Toggle grid snapping with configurable intervals or BPM-based grid.

### 🛠 Easy Editing
- **Double-Click to Edit**: Open a dialog to change event type, target, color, and duration.
- **Color Coding**: Each event type has a default category color, or customized freely.
- **Persistence**: **Import/Export** your full timeline to JSON for backup or sharing.
- **Drag & Drop**: Freely move events on the timeline (with optional snap).
- **Keyboard Shortcuts**:
  - `Space`: Play/Pause
  - `Delete`: Remove selected event
  - `Home`: Jump to start
  - `Arrows`: Nudge events (Shift+Arrows for fine control)
  - `Ctrl+C` / `Ctrl+V`: Copy/Paste events

## Installation

### Linux (Manual Build)

1.  **Prerequisites**:
    -   OBS Studio (installed via PPA or built from source)
    -   CMake 3.16+
    -   Qt6 Development packages
    -   Build Essentials (`build-essential`)

2.  **Build**:
    ```bash
    cmake -S . -B build
    cmake --build build
    ```

3.  **Install**:
    ```bash
    cmake --install build
    ```
    This will install the plugin to `~/.config/obs-studio/plugins/timeline/`.

## Planned Features

- **Undo/Redo**: Add a history stack for reverting timeline changes.

- **Multi-Selection**: Support box selection and shift-clicking for moving multiple events at once.

- **Unified Visibility Events**: Merge `Source Show/Hide` and `Filter Enable/Disable` into unified `Source Visibility` and `Filter Visibility` event types with an internal toggle state.

## Usage

1.  **Open the Dock**: Go to **View** > **Docks** and select **Timeline**.
2.  **Add Event**: Click **Add Event** to place a marker at the playhead.
3.  **Configure**: Double-click the marker to set its type (e.g., Hotkey, Scene Switch) and target.
4.  **Refine**: Drag to position, right-click to set color, or use the **Snap** checkbox for precision.
5.  **Play**: Click Play or press Spacebar to watch it happen!

## Architecture

For a comprehensive technical reference covering the plugin's architecture, design decisions, data models, and integration points, see:

📄 **[docs/ARCHITECTURE.md](docs/ARCHITECTURE.md)**

### Component Overview

```
┌─────────────────────────────────────────────────────────────┐
│                    OBS Studio Process                        │
│  ┌───────────────────────────────────────────────┐          │
│  │             OBS Frontend Thread (Qt UI)               │  │
│  │  ┌─────────────┐  ┌──────────────┐  ┌─────────────┐ │  │
│  │  │ TimelineDock│  │TimelineModel │  │  Scheduler  │ │  │
│  │  │  (QWidget)  │  │  (Data)      │  │  (QTimer)   │ │  │
│  │  └──────┬──────┘  └──────┬───────┘  └──────┬──────┘ │  │
│  │         │                │                 │         │  │
│  │  ┌──────▼────────────────▼─────────────────▼──────┐  │  │
│  │  │              ObsExecutor                        │  │  │
│  │  │         (OBS C API Bridge)                      │  │  │
│  │  └──────────────────┬─────────────────────────────┘  │  │
│  │                     │                                 │  │
│  │  ┌──────────────────▼─────────────────────────────┐  │  │
│  │  │           libobs / obs-frontend-api            │  │  │
│  │  └────────────────────────────────────────────────┘  │  │
│  └───────────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────────┘
```

| Component | File | Responsibility |
|-----------|------|--------------|
| `TimelineModel` | `src/timeline-model.cpp` | Event data store, sorting, JSON serialization |
| `TimelineScheduler` | `src/timeline-scheduler.cpp` | Playback timing, event firing, loop logic |
| `ObsExecutor` | `src/obs-executor.cpp` | OBS API bridge — scene switches, source visibility, media, filters, hotkeys |
| `TimelineDock` | `src/timeline-dock.cpp` | Main UI widget — QGraphicsView scene, controls, persistence |
| `TimelineEventItem` | `src/timeline-event-item.cpp` | Draggable graphics items on the timeline |
| `HeadlessTimeline` | `src/headless-timeline.cpp` | Nested timeline runner (no UI) |

### Development

-   **Source**: `src/` contains all C++ source code.
-   **UI**: Built with Qt6 Widgets and QGraphicsView.
-   **Tests**: `tests/` contains unit tests with mocked OBS API.

### Build Commands

```bash
# Configure and build
cmake -S . -B build
cmake --build build

# Install
cmake --install build

# Run tests
cd build && ./tests/run_tests -v
```

## Contributing

See the root `AGENTS.md` for developer conventions, critical rules (memory management, threading), and task-oriented routing to folder-specific guides.

## License

GPL-2.0-or-later
