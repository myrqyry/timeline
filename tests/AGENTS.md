# tests/

## What Lives Here

Qt Test-based unit tests for the timeline plugin. Tests use a mock OBS environment (dummy source types, mock frontend API, mock websocket vendor) so they can run headless without a full OBS installation.

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

- Tests compile all production sources except `plugin-main.cpp`: `obs-executor.cpp`, `headless-timeline.cpp`, `timeline-scheduler.cpp`, `timeline-model.cpp`, `timeline-event-item.cpp`, `timeline-serialization.cpp`, `macro-button-model.cpp`, `macro-button-widget.cpp`, `macro-serialization.cpp`, `macro-strip-dock.cpp`, and `event-editor.cpp` (`tests/CMakeLists.txt:13-29`). Changes to any of these files may break tests.
- Mock state is reset in `cleanup()` between tests. New tests that rely on frontend/websocket state must initialize mocks in `init()` or `initTestCase()`.

## Watch Out For

- **Dummy source registration**: Tests register fake source types (`scene`, `cut_transition`, `test_source`, `test_filter`) in `initTestCase()`. These are minimal — they only allocate/free memory. Do not expect full scene/transition behavior.
- **Offscreen platform**: `QT_QPA_PLATFORM=offscreen` is set in `main()` so tests run headless. GUI interactions that require a real display may fail.
- **Memory leak assertion**: `cleanupTestCase()` asserts on `bnum_allocs() < 4` after mocking all globals and draining OBS source enumeration. Up to 3 residual allocations from OBS source-type registration metadata are expected in headless test environments; a count of 4+ indicates a regression in source reference cleanup.
- **OBS startup failure**: `obs_startup()` can fail in some CI environments. Tests call `QSKIP()` if sources cannot be created.

## Commands (Folder-Specific Only)

```bash
# Run all tests (from build directory)
cd build && ./tests/run_tests

# Run a specific test function (use -v1 for start-of-test logging)
cd build && ./tests/run_tests testSceneSwitch

# Run tests matching a pattern
cd build && ./tests/run_tests testMedia*
```
