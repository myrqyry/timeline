# tests/

## What Lives Here

Qt Test regression tests for the timeline plugin. The harness links against real libobs, initializes OBS and an OpenGL context, and substitutes dummy source types plus mock frontend and WebSocket APIs. It requires OBS development libraries; running headlessly still needs a working graphics context (usually Xvfb with software OpenGL).

## Key Files

| File | Purpose |
|------|---------|
| `main_test.cpp` | QTest entry point — `TestTimeline` class with `testSceneSwitch`, `testMediaStart`, `testSourceVisibility`, `testFilterEnable` |
| `mock_frontend.cpp` | Mock implementations of `obs_frontend_*` functions |
| `mock_obs_websocket.cpp` | Mock implementation of `obs_websocket_*` functions |
| `CMakeLists.txt` | Test target build config — links `run_tests` executable against plugin sources and Qt Test |

## Deviations from Root

No deviations — follow root conventions. Tests use the same memory rules (`bzalloc`, `bfree`) and logging macros as production code.

## Dependencies & Side Effects

- [`TEST_SOURCES`](CMakeLists.txt) is the build authority. The harness compiles the real docks, timecode display and automation recorder, but omits `plugin-main.cpp` and substitutes frontend and WebSocket functions.
- Mock state is reset in `cleanup()` between tests. New tests that rely on frontend/websocket state must initialize mocks in `init()` or `initTestCase()`.

## Watch Out For

- **Fixture scope**: The harness uses real libobs scenes and test sources with minimal callbacks. Frontend test doubles do not reproduce native OBS transitions, third-party sources or rendered output.
- **Headless graphics**: `QT_QPA_PLATFORM=offscreen` is the local fallback, but strict tests run with `QT_QPA_PLATFORM=xcb` under Xvfb and software GL; do not count OpenGL/OBS startup skips as coverage.
- **Memory leak assertion**: `cleanupTestCase()` asserts on `bnum_allocs() < 4` after mocking all globals and draining OBS source enumeration. Up to 3 residual allocations from OBS source-type registration metadata are expected in headless test environments; a count of 4+ indicates a regression in source reference cleanup.
- **OBS runtime availability**: Local tests can skip when headless OpenGL or OBS startup is unavailable, but `TIMELINE_REQUIRE_OBS_RUNTIME=1` makes that a hard failure. The Linux CI workflow enables this flag under Xvfb; skipped runtime tests must never count as a green required CI job.

## Commands (Folder-Specific Only)

```bash
# Strict full suite from the project root (requires xvfb-run and software GL):
TIMELINE_REQUIRE_OBS_RUNTIME=1 QT_QPA_PLATFORM=xcb \
  LIBGL_ALWAYS_SOFTWARE=1 xvfb-run -a \
  ctest --test-dir build_x86_64 --output-on-failure

# Focused Qt test in the same strict environment:
TIMELINE_REQUIRE_OBS_RUNTIME=1 QT_QPA_PLATFORM=xcb \
  LIBGL_ALWAYS_SOFTWARE=1 xvfb-run -a \
  build_x86_64/tests/run_tests testSceneSwitch
```
