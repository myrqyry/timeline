# OBS Timeline Plugin Packaging

This document describes how to build and package `obs-timeline-plugin` for local installation in OBS.

## Prerequisites

- CMake 3.16+
- C++17 toolchain
- Qt6 (`Core`, `Gui`, `Widgets`, `Svg`)
- `libobs` development headers/libraries
- (Optional) `obs-websocket-api.h` for websocket vendor support

## Configure

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
```

Optional flags:

- `-DENABLE_OBS_WEBSOCKET_VENDOR=ON|OFF`
- `-DOBS_TIMELINE_PLUGIN_ROOT=/path/to/plugins/obs-timeline-plugin`
- `-DOBS_PLUGIN_DESTINATION=/custom/bin/64bit`
- `-DOBS_DATA_DESTINATION=/custom/data`

## Build

```bash
cmake --build build
```

## Install

```bash
cmake --install build
```

Default install layout:

- Binary: `~/.config/obs-studio/plugins/obs-timeline-plugin/bin/64bit`
- Data: `~/.config/obs-studio/plugins/obs-timeline-plugin/data`

## Quick Validation

1. Launch OBS.
2. Open `View > Docks > Timeline`.
3. Load one of the example timeline JSON files from `examples/`.
4. Verify playback controls and event execution.
