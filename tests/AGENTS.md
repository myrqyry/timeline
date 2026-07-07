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

- Tests compile `src/obs-executor.cpp`, `src/timeline-model.cpp`, `src/timeline-scheduler.cpp`, `src/timeline-event-item.cpp`, and `src/headless-timeline.cpp` directly. Changes to these files may break tests.
- Mock state is reset in `cleanup()` between tests. New tests that rely on frontend/websocket state must initialize mocks in `init()` or `initTestCase()`.

## Watch Out For

- **Dummy source registration**: Tests register fake source types (`scene`, `cut_transition`, `test_source`, `test_filter`) in `initTestCase()`. These are minimal — they only allocate/free memory. Do not expect full scene/transition behavior.
- **Offscreen platform**: `QT_QPA_PLATFORM=offscreen` is set in `main()` so tests run headless. GUI interactions that require a real display may fail.
- **Memory leak noise**: `cleanupTestCase()` reports leaks via `bnum_allocs()` but does not assert on them (commented out) because mock shutdown is not perfect.
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
