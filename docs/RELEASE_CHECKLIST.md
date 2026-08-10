# Release Checklist

Use this checklist before tagging a release.

## Build & Install

- [ ] Clean configure: `cmake --preset ubuntu-x86_64`
- [ ] Clean build: `cmake --build --preset ubuntu-x86_64`
- [ ] Clean install: `cmake --install build_x86_64`

## Automated Tests

- [ ] Run the test suite: `ctest --test-dir build_x86_64 --output-on-failure`
- [ ] Run a focused test when diagnosing a regression:
  `build_x86_64/tests/run_tests testSceneSwitch`

## OBS Runtime Smoke

- [ ] OBS launches without plugin crash
- [ ] Dock appears in `View > Docks > Timeline`
- [ ] Add event works
- [ ] Drag event works
- [ ] Save/load works

## Event Execution

- [ ] Scene switch works
- [ ] Source visibility works
- [ ] Filter enable/disable works
- [ ] Media start/stop works
- [ ] Hotkey trigger works
- [ ] Trigger timeline works

## Timeline Behavior

- [ ] Loop works
- [ ] Duration start/end behavior works
- [ ] Pause/resume preserves duration behavior
- [ ] Seek before/after duration boundaries behaves correctly

## Compatibility

- [ ] Build and test the OBS 31.1.2 artifact with `TIMELINE_OBS_COMPATIBILITY=obs31`
- [ ] Build and test the OBS 32.2.1 artifact with `TIMELINE_OBS_COMPATIBILITY=obs32`
- [ ] Verify each artifact is labeled with its OBS compatibility target
- [ ] Verify the OBS 31 macOS package targets macOS 12 or newer
- [ ] Verify the OBS 32 macOS package targets macOS 13 or newer
- [ ] Legacy array-only JSON loads
- [ ] Object JSON with settings loads
- [ ] Invalid JSON fails gracefully (warning shown/logged)

## Optional Websocket Vendor

- [ ] Build with `ENABLE_OBS_WEBSOCKET_VENDOR=ON` (header available)
- [ ] Build with `ENABLE_OBS_WEBSOCKET_VENDOR=OFF`
- [ ] Runtime works when websocket vendor support is disabled
