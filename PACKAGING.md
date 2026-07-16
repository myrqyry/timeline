# OBS Timeline Plugin Packaging

This document describes how to build and package the **Timeline for OBS**
plugin (project name: `timeline`) for local installation in OBS Studio.

## Prerequisites

- CMake 3.16+
- C++17 toolchain
- Qt6 (`Core`, `Gui`, `Widgets`, `Svg`)
- `libobs` development headers/libraries (via `pkg-config`)
- (Optional) `obs-websocket-api.h` for WebSocket vendor support

## Configure

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
```

Optional flags:

- `-DENABLE_OBS_WEBSOCKET_VENDOR=ON|OFF` — enable or disable WebSocket vendor
  support. When enabled, CMake uses the vendor header if it is available;
  when disabled, the integration is compiled out.
- `-DTIMELINE_PLUGIN_ROOT=/path/to/install/root` — override the
  install location.
- `-DOBS_PLUGIN_DESTINATION=/custom/bin/64bit` — override the binary
  destination.
- `-DOBS_DATA_DESTINATION=/custom/data` — override the data
  destination.

## Build

```bash
cmake --build build
```

## Install

```bash
cmake --install build
```

Default install layout (Linux):

- Binary: `~/.config/obs-studio/plugins/timeline/bin/64bit/timeline.so`
- Data: `~/.config/obs-studio/plugins/timeline/data/`

## Quick Validation

1. Launch OBS.
2. Open **View > Docks > Timeline** (and optionally **Macro Strip**).
3. Add a test event, save the scene collection, and verify playback controls
   and event execution.
4. Stop OBS, then verify the file
   `~/.config/obs-studio/plugin_config/timeline/timeline.json` contains
   your saved timeline settings and events.
5. If you use Macro Strip, verify
   `~/.config/obs-studio/plugins/timeline/data/macro_buttons.json` contains
   the saved buttons.
