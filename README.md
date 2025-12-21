# OBS Timeline Sequencer

A timeline-based sequencer plugin for OBS Studio. This plugin allows you to schedule and automate events, such as scene switching, using a visual timeline interface.

## Features

-   **Visual Timeline Dock**: A dedicated, resizable dock in OBS (View -> Docks -> Timeline) to visualize and manage automation.
-   **Event Scheduling**: Place events on the timeline to trigger actions at specific times.
    -   *Current implementation supports Scene Switching.*
-   **Playback Control**: Play, Pause, and Scrub through the timeline.
    -   Click the "Play" button to start automation.
    -   Click on the timeline track to scrub/seek.
-   **Zooming**: Hold `Ctrl` and scroll the mouse wheel to zoom in and out of the timeline.
-   **Persistence**: The timeline state (events and positions) is automatically saved and loaded with your OBS profile.

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
2.  **Add Event**: Click the "Add Event" button. A new event will appear at the current playhead position + 2 seconds.
3.  **Move Event**: Drag the red event block to retime it.
4.  **Edit Event**: *Coming Soon* (Currently defaults to switching to "Scene 2").
5.  **Zoom**: Hover over the timeline, hold `Ctrl`, and scroll to zoom.

## Development

-   **Source**: `src/` contains all C++ source code.
-   **UI**: Built with Qt Widgets and QGraphicsView.
-   **Persistence**: Uses `obs_frontend_add_save_callback` to save `timeline_events.json` in the plugin config directory.

## License

GPL-2.0-or-later
