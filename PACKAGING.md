# OBS Timeline Plugin Packaging

This document describes how to build and package the **Timeline for OBS**
plugin (project name: `timeline`) for local installation in OBS Studio.

## Prerequisites

- CMake 3.28+
- C++17 toolchain
- Qt6 (`Core`, `Gui`, `Widgets`, `Svg`, and `Test` for the default test build)
- Ninja for the Ubuntu CMake presets
- Matching `libobs` and `obs-frontend-api` development headers and libraries
  discoverable by CMake
- (Optional) `obs-websocket-api.h` for WebSocket vendor support

## Configure

The build produces one binary for one OBS major version. Select `obs31`,
`obs32`, or `obs33` before configuring. Use a separate build directory per
target; never install binaries for different OBS majors over one another.

```bash
# Using CMake presets (recommended):
cmake --preset ubuntu-x86_64 -DTIMELINE_OBS_COMPATIBILITY=obs32

# Or without presets:
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DTIMELINE_OBS_COMPATIBILITY=obs32
```

The OBS 31.1.2 and OBS 32.2.1 targets have established cross-platform builds.
OBS 33.0.1 is a **Linux x86_64 and Windows x64 compatibility target**: its source archive is
SHA-256 pinned. The [Linux CI run on b7eb40b](https://github.com/myrqyry/timeline/actions/runs/44660008317)
passed all 102 strict headless tests for OBS33 on Ubuntu 26.04. A separate
module build against the installed OBS33 Flatpak's libobs and Qt 6.11.1
headers, libraries and matching moc loaded both docks and executed a
scheduled scene change and a Macro Strip return action in a disposable
OBS33 profile. This local probe used the host C++ compiler, not the
complete Flatpak SDK; it is not a distributable Flatpak package build.
Hosted build status and remaining native checks are tracked in the
[release checklist](docs/RELEASE_CHECKLIST.md).

Use Ubuntu 26.04 or a matching SDK environment, not Qt 6.4 host libraries
with OBS33. The Linux CI workflow builds the selected pinned OBS SDK
from source. A local build uses the installed SDK unless
`TIMELINE_BUILD_OBS_FROM_SOURCE=ON` is set.

Optional flags:

- `-DENABLE_OBS_WEBSOCKET_VENDOR=ON|OFF` — enable or disable WebSocket vendor
  support. When enabled, CMake uses the vendor header if it is available;
  when disabled, the integration is compiled out.
- `-DTIMELINE_SYSTEM_INSTALL=ON` — use the matching Linux system plugin
  directories (CI enables this; defaults to OFF).
- `-DTIMELINE_PLUGIN_ROOT=/path/to/install/root` — override the
  install location.
- `-DOBS_PLUGIN_DESTINATION=/custom/bin/64bit` — override the binary
  destination.
- `-DOBS_DATA_DESTINATION=/custom/data` — override the data
  destination.
- `-DTIMELINE_OBS_COMPATIBILITY=obs31|obs32|obs33` — select the OBS major version.
- `-DTIMELINE_BUILD_OBS_FROM_SOURCE=ON` — build the selected OBS SDK from its
  pinned source archive on Linux.
- `-DTIMELINE_BUILD_TESTS=OFF` — omit the Qt Test suite and its `Qt6::Test`
  dependency; tests are enabled by default.

## Build

```bash
# When using presets:
cmake --build --preset ubuntu-x86_64
# Or without presets:
cmake --build build
```

### Windows OBS33 x64

Use Visual Studio 2026 with the C++ desktop workload, Windows SDK
10.0.26100.0 and CMake 4.2+ (the Visual Studio 18 generator). The dedicated
preset pins OBS 33.0.1 and the official 2026-10-23 OBS/Qt dependency bundles.
The Windows CI runner is windows-2025-vs2026.

From a developer PowerShell:

    cmake --preset windows-obs33-x64 -DTIMELINE_BUILD_TESTS=OFF
    cmake --build --preset windows-obs33-x64
    cmake --install build_obs33_x64 --config RelWithDebInfo

The default target is `%ALLUSERSPROFILE%/obs-studio/plugins/timeline`
(normally under `%ProgramData%`), with `timeline.dll` next to `data/`.
Installing there may require
administrator privileges. Close OBS and back up any existing Timeline
installation first; do not leave an older DLL in another discoverable
plugin location. CI stages each major separately and produces a
major-labeled ZIP; it checks the actual archive for the expected layout.
OBS33 ZIPs contain only the modern DLL path. OBS31/32 ZIPs include both
the legacy `bin/64bit` DLL and the modern root DLL.

See the [release checklist](docs/RELEASE_CHECKLIST.md) for verification gates.
The legacy release-package matrix has no macOS OBS33 or Windows ARM64 target. The additive shared qualification workflow separately configures OBS33 macOS ARM64 and Intel test binaries; hosted qualification remains open.

## Install

```bash
# When using presets:
cmake --install build_x86_64
# Or without presets:
cmake --install build
```

Default user install layouts (Linux, use a fresh build directory per major):

| Target | Binary relative to the user plugin directory | User plugin directory |
| --- | --- | --- |
| OBS31/32 | `timeline/bin/64bit/timeline.so` and `timeline/timeline.so` | `~/.config/obs-studio/plugins` |
| OBS33 | `timeline/timeline.so` | `$XDG_DATA_HOME/obs-studio/plugins` (fallback `~/.local/share/obs-studio/plugins`) |

On Windows, OBS31/32 default to `%APPDATA%/obs-studio/plugins/timeline` and
also install both binary paths. See [Windows OBS33 x64](#windows-obs33-x64)
for the system-wide OBS33 default.

These plugin layouts use `timeline/data/locale/en-US.ini` and preserve writable
configuration in the OBS module config directory. OBS33 Flatpak uses
its sandbox data directory (normally
`~/.var/app/com.obsproject.Studio/data/obs-studio/plugins` on the host).
Build against the matching Flatpak runtime/SDK for that installation;
do not install an Ubuntu Debian artifact into the Flatpak. Close the
target OBS process before installing.

OBS 31 macOS packages require macOS 12 or newer. OBS 32 macOS packages require
macOS 13 or newer.

### CI artifacts and release-mode packages

GitHub Actions builds **separate OBS 31 and OBS 32 variants** for Linux,
Windows x64 and macOS universal. The packaging matrix also includes OBS33
on Ubuntu 26.04 and on the `windows-2025-vs2026` x64 runner. Each target
provides a major-labeled archive or Debian package. The
CI matrix also exercises the Debian `package` target during pull requests so
release-only packaging failures are caught before tagging.

The additive [shared qualification workflow](.github/workflows/shared-ci.yml)
also configures OBS 32.2.0 and 33.0.3 test binaries for Linux x86_64, Windows
x64, macOS ARM64 and macOS Intel. These use a separate pinned SDK driver and
verified payload format, not the legacy release archives. See
[shared CI details](.github/scripts/SHARED-CI.md). Current candidate hosted
results and native runtime loading remain unverified.

- OBS31/32 Debian packages retain `/usr/lib/obs-plugins/` and
  `/usr/share/obs/obs-plugins/timeline/`. OBS33 uses
  `/usr/lib/x86_64-linux-gnu/obs-modules/plugins/` and
  `/usr/share/obs/obs-modules/plugins/timeline/` on the Ubuntu x86_64
  runner, following the modern loader in OBS 33.0.1. All packages require
  the matching OBS major version. These are system, not per-user, layouts.
- Local user-plugin and system-wide package installs target the same OBS
  module name. Do not install two OBS major variants simultaneously.
- The repository's `data/AGENTS.md` documents development conventions and is
  **not** an OBS runtime asset; it must not be included in install packages.
- CPack source archives are built from the working tree, **not** Git's tracked
  files. `.gitignore` alone does not protect private `.meristem/local/` state;
  CPack excludes it explicitly. All Ubuntu CI jobs seed synthetic local data
  before packaging and refuse to upload an archive that contains that path.
- Compiled CI artifacts are **test candidates**. Actual Windows/macOS OBS
  loading and interactive behavior remain unverified; see
  [the release checklist](docs/RELEASE_CHECKLIST.md). No tagged release exists
  yet.

## Quick Validation

1. Launch OBS with an isolated test profile and scene collection.
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
