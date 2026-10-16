# OBS Timeline Plugin Packaging

This document describes how to build and package the **Timeline for OBS**
plugin (project name: `timeline`) for local installation in OBS Studio.

## Prerequisites

- CMake 3.28+
- C++17 toolchain
- Qt6 (`Core`, `Gui`, `Widgets`, `Svg`)
- `libobs` development headers/libraries (via `pkg-config`)
- (Optional) `obs-websocket-api.h` for WebSocket vendor support

## Configure

The build produces one binary for one OBS major version. Select `obs31` or
`obs32` before configuring, and use separate build directories for the two
targets.

```bash
# Using CMake presets (recommended):
cmake --preset ubuntu-x86_64 -DTIMELINE_OBS_COMPATIBILITY=obs32

# Or without presets:
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DTIMELINE_OBS_COMPATIBILITY=obs32
```

The supported targets are OBS 31.1.2 and OBS 32.2.1. CI builds the matching
OBS SDK from source. A local build uses the installed SDK unless
`TIMELINE_BUILD_OBS_FROM_SOURCE=ON` is set.

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
- `-DTIMELINE_OBS_COMPATIBILITY=obs31|obs32` — select the OBS major version.
- `-DTIMELINE_BUILD_OBS_FROM_SOURCE=ON` — build the selected OBS SDK from its
  pinned source archive on Linux.

## Build

```bash
# When using presets:
cmake --build --preset ubuntu-x86_64
# Or without presets:
cmake --build build
```

## Install

```bash
# When using presets:
cmake --install build_x86_64
# Or without presets:
cmake --install build
```

Default install layout (Linux):

- Binary: `~/.config/obs-studio/plugins/timeline/bin/64bit/timeline.so`
- Data: `~/.config/obs-studio/plugins/timeline/data/`

OBS 31 macOS packages require macOS 12 or newer. OBS 32 macOS packages require
macOS 13 or newer.

### CI artifacts and release-mode packages

GitHub Actions builds **separate OBS 31 and OBS 32 variants** for Linux,
Windows x64 and macOS universal. The Linux build provides an `obs31`/`obs32`
archive and, when packaging is enabled, a version-labeled Debian package. The
CI matrix also exercises the Debian `package` target during pull requests so
release-only packaging failures are caught before tagging.

- The Linux `.deb` installs `timeline.so` into `/usr/lib/obs-plugins/` and
  localization into `/usr/share/obs/obs-plugins/timeline/`; it depends on
  the matching `obs-studio` major version. This differs from the default
  local user-plugin install above.
- Local user-plugin and system-wide package installs target the same OBS
  module name. Do not install two OBS major variants simultaneously.
- The repository's `data/AGENTS.md` documents development conventions and is
  **not** an OBS runtime asset; it must not be included in install packages.
- Compiled CI artifacts are **test candidates**. Actual Windows/macOS OBS
  loading and interactive behavior remain unverified; see
  [the release checklist](docs/RELEASE_CHECKLIST.md). No tagged release exists
  yet.

## Quick Validation

1. Launch OBS.
2. Open **View > Docks > Timeline** (and optionally **Macro Strip**).
3. Add a test event, save the scene collection, and verify playback controls
   and event execution.
4. Stop OBS, then verify the file
   `~/.config/obs-studio/plugin_config/timeline/timeline.json` contains
   your saved timeline settings and events.
5. If you use Macro Strip, verify
   `~/.config/obs-studio/plugin_config/timeline/macro_buttons.json` contains
   the saved buttons (Linux default). Older Linux files in the plugin's
   installed `data/` directory are imported without deleting the original.
