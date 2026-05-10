# OBS Timeline Plugin

An OBS Studio dock plugin that adds a timeline scheduler for automating scene switches, source visibility, media playback, filter toggles, hotkeys, and nested timeline triggers.

**Tech stack:** C++17, CMake 3.16+, Qt6, libobs (Pure C API)

## Loading Protocol

When entering a directory or switching context to a specific file path:
1. Check if the current or parent directory contains an `AGENTS.md` file.
2. If found, read it immediately before performing any actions in that directory.
3. Treat the instructions in the local `AGENTS.md` as overrides for this root file.

## Critical Rules

### Rule 0: Verify New Code is Running
Before debugging logic, prove you are running the new code:
1. Add `blog(LOG_INFO, "DEBUG_ACTIVE_XYZ123");` with a random ID.
2. If you do not see that ID in logs, STOP — solve the build/install path first.
3. Check for zombie installs: `ls -R /usr/lib/obs-plugins /usr/local/lib/obs-plugins`

### Memory Management
- Never use `malloc`/`calloc`/`free`. Use `bmalloc`, `bzalloc`, `bfree`.
- Mixing `std::malloc` with `bfree` crashes immediately.
- Strings passed to OBS that must persist: use `bstrdup()`; free with `bfree()`.

### Threading
- OBS has three pipelines: `obs_graphics_thread`, `video_thread`, `audio_thread`.
- Never block the graphics thread. No file I/O in `video_render`.
- No GPU operations in `video_tick()`. Use `os_atomic_*` instead of `pthread_*`.

### Module Entry
Every plugin entry file (`plugin-main.cpp`) must contain:
```c
OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("obs-timeline-plugin", "en-US")
```

## Quick Commands

```bash
# Configure, build, and install locally
cmake -S . -B build
cmake --build build
cmake --install build

# Run tests
cd build && ./tests/run_tests -v
cd build && ./tests/run_tests testSceneSwitch

# Format code
clang-format -i src/your-file.cpp
find src -name "*.cpp" -o -name "*.h" | xargs clang-format -i
```

## Shared Conventions

### Code Style
- Spaces only, 4-space indent, max line length 100 (prefer 80).
- Includes order: main header → system/external → project headers (alphabetical).
- Naming: `PascalCase` classes, `camelCase` functions/variables, `mCamelCase` private members, `snake_case` files.
- Types: `double` for milliseconds, `QString` for Qt strings, `obs_*` types for OBS.

### Logging
- Use `blog(LOG_LEVEL, ...)` only. Levels: `LOG_DEBUG`, `LOG_INFO`, `LOG_WARNING`, `LOG_ERROR`.
- Never use `printf` or `std::cout`; they do not appear in OBS logs.

### Build
- Source files are listed manually in `CMakeLists.txt`. Add new `.cpp` files there.
- Headers are listed for IDE convenience.
- Do not edit files in `cmake/common/`.

### Versioning
- Never manually edit version numbers in source files.
- Edit `version` in `CMakeLists.txt` line 3: `project(timeline VERSION 1.0.0)`.

## Routing

When working on a specific task, read the folder's `AGENTS.md` first:

- **Building or modifying the dock UI, timeline rendering, event items, or OBS integration** → `src/AGENTS.md`
- **Writing or fixing unit tests, mocks, or test infrastructure** → `tests/AGENTS.md`
- **Adding or updating localization strings or data assets** → `data/AGENTS.md`
