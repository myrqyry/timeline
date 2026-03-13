# OBS Timeline Sequencer

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
- **Smart Contrast**: Icons automatically switch between Black and White for perfect visibility on any background color.
- **Smart Layout**: Events placed at the same time automatically **stack vertically**.
- **Zoomable**: Hold `Ctrl` + Scroll to zoom from 5 seconds to 10 minutes view.

### ⚡ Powerful Automation
- **8 Event Types**:
  1. **Scene Switch**: Cut to a specific scene.
  2. **Source Visibility**: Show/Hide specific sources in a scene.
  3. **Media Control**: Start/Stop media sources.
  4. **Filter Control**: Enable/Disable filters on sources.
  5. **Hotkey Trigger**: Trigger any registered OBS hotkey.
- **Looping**: Set a timeline end time and loop indefinitely for complex background animations.
- **Snap to Grid**: Toggle grid snapping for precise 100ms alignment.

### 🛠 Easy Editing
- **Double-Click to Edit**: Open a dialog to change event type, target, and color.
- **Color Coding**: Each event type has a default category color, or customized freely.
- **Persistence**: **Import/Export** your full timeline to JSON for backup or sharing.
- **Drag & Drop**: Freely move events on the timeline (with optional snap).
- **Keyboard Shortcuts**:
  - `Space`: Play/Pause
  - `Delete`: Remove selected event
  - `Home`: Jump to start
  - `Arrows`: Nudge events (Shift+Arrows for fine control)

## Installation

### Linux (Manual Build)

1.  **Prerequisites**:
    -   OBS Studio (installed via PPA or built from source)
    -   CMake 3.20+
    -   Qt6 Development packages
    -   Build Essentials (`build-essential`)

2.  **Build**:
    ```bash
    mkdir build && cd build
    cmake ..
    cmake --build .
    ```

3.  **Install**:
    ```bash
    cmake --install .
    ```
    This will install the plugin to `~/.config/obs-studio/plugins/obs-timeline-plugin/`.

## Usage

1.  **Open the Dock**: Go to **View** > **Docks** and select **Timeline**.
2.  **Add Event**: Click **Add Event** to place a marker at the playhead.
3.  **Configure**: Double-click the marker to set its type (e.g., Hotkey, Scene Switch) and target.
4.  **Refine**: Drag to position, right-click to set color, or use the **Snap** checkbox for precision.
5.  **Play**: Click Play or press Spacebar to watch it happen!

## Development

-   **Source**: `src/` contains all C++ source code.
-   **UI**: Built with Qt Widgets and QGraphicsView.
-   **Architecture**:
    -   `TimelineModel`: Manages event data.
    -   `TimelineView` / `Dock`: Handles UI and rendering.
    -   `ObsExecutor`: Interfaces with OBS API to trigger actions.
-   **API Skill**: Includes `obs-frontend-dev/` skill for OBS frontend plugin development patterns (dock lifecycle, WebSocket vendors, settings persistence).

### Build Commands

```bash
cmake -S . -B build \
  -DCMAKE_INSTALL_PREFIX="$HOME/.config/obs-studio/plugins/obs-timeline-plugin" \
  -DOBS_PLUGIN_DESTINATION="bin/64bit" \
  -DOBS_DATA_DESTINATION="data"

cmake --build build
cmake --install build
```

### Running Tests

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
cd build && ctest --verbose
```

## License

GPL-2.0-or-later
