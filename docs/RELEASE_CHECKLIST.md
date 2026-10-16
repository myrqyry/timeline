# Release Checklist

Use this checklist for a **specific commit** before publishing a tag or asking
streamers to install Timeline. Passing CI validates compiled artifacts and the
test harness, not interactive behavior on every operating system.

## Verified baseline (2026-10-16)

The last fully verified pre-cleanup commit is `33a15f5` on
`aster/runtime-hardening`:

- [Linux Build — OBS 31 and 32](https://github.com/myrqyry/timeline/actions/runs/38855074260):
  both builds and strict Qt tests passed, with no skipped tests.
- [Cross-platform build matrix](https://github.com/myrqyry/timeline/actions/runs/38855074267):
  Linux, Windows and macOS for OBS 31 and 32, including formatting and Linux
  Debian release-mode packaging, all passed.
- All five earlier Codex review threads and the later OBS config-path review
  thread were resolved. The config-path finding was a false positive:
  `obs_module_config_path(file)` is an OBS-provided macro.
- Isolated **real OBS 32 on Linux** loaded both docks and, through OBS WebSocket,
  demonstrated source hide, duration restore and scene switch. A separate
  isolated startup verified Macro Strip migration and canonical-file precedence.
- The CI-produced OBS 31/32 Debian packages were downloaded and inspected:
  `timeline.so` and `locale/en-US.ini` are correctly installed and
  `obs-studio` dependencies are bounded to the appropriate major version.

This is an evidence record, **not approval to tag**. Repeat the relevant checks
after additional commits. Windows/macOS **runtime installation and interactive
use have not been demonstrated**.

## Before tagging (release blockers)

- [ ] Confirm the exact commit, its six build jobs, two Linux test jobs and
  formatting checks are green. Do not rely on an earlier green SHA.
- [ ] Inspect/download the **actual CI artifacts**: OBS 31/32 for Linux,
  Windows and macOS. Confirm they match the intended OBS major version.
- [ ] Inspect Linux `.deb` payloads and dependency fields, including the
  `obs-studio` major-version bounds; ensure no `AGENTS.md` or downloaded
  SDK/source trees are in the runtime packages.
- [ ] Confirm the source archive contains no `.deps` SDK downloads and no
  generated build/release outputs.
- [ ] Run clean install and upgrade tests using **temporary OBS profiles**,
  not the maintainer's everyday scene collections.
- [ ] Test the actual plugin interactively on Windows and macOS before claiming
  either platform is runtime-verified.
- [ ] Confirm release artifacts, tag/version naming and checksums. No tag or
  publication until the remaining checks are reviewed.

## Local Linux build and tests

Select the target matching your installed OBS major version. Use a separate
build tree when changing SDK targets.

```bash
cmake --preset ubuntu-x86_64 -DTIMELINE_OBS_COMPATIBILITY=obs32
cmake --build --preset ubuntu-x86_64

# Real OBS startup needs an OpenGL context. Xvfb + software GL is the CI path.
TIMELINE_REQUIRE_OBS_RUNTIME=1 QT_QPA_PLATFORM=xcb \
  LIBGL_ALWAYS_SOFTWARE=1 xvfb-run -a \
  ctest --test-dir build_x86_64 --output-on-failure

# Optional focused diagnosis:
TIMELINE_REQUIRE_OBS_RUNTIME=1 QT_QPA_PLATFORM=xcb \
  LIBGL_ALWAYS_SOFTWARE=1 xvfb-run -a \
  build_x86_64/tests/run_tests testSceneSwitch
```

Without the strict environment flag, local headless tests can report a skip
if OpenGL or OBS startup is unavailable. **Skipped tests are not a passing
runtime check**. Run the OBS 31 suite with `obs31` in its own build tree.

## Interactive OBS verification

- [ ] OBS loads the plugin and exposes **Timeline** and **Macro Strip** under
  View → Docks without a crash or startup error.
- [ ] Add, edit, drag, duplicate/copy, delete and paste events. Verify grid
  snapping, zoom, duration handles and playhead seeking.
- [ ] Save/restart/reload both timeline and Macro Strip state; import valid and
  invalid JSON. Corrupt imports must not erase current data.
- [ ] Verify Macro Strip migration from legacy Linux storage, scene-collection
  backup fallback, and protection of newer or unsaved edits.
- [ ] Trigger scene switch (default and named temporary transition), source
  show/hide, filter enable/disable, media start/stop, OBS hotkey and nested
  timeline events against actual OBS sources.
- [ ] Stop a parent timeline while a nested temporary transition is active:
  OBS must return to the original transition.
- [ ] Verify looping, pause/resume, seeking across duration boundaries and
  stopping during an active duration event.
- [ ] Check WebSocket-off builds run normally; independently test vendor
  integration only if the proper `obs-websocket-api.h` was available at build
  time. Do not confuse the optional vendor integration with the OBS WebSocket
  used by the integration smoke check.

## Platform/install notes

- OBS 31.1.2 and 32.2.1 are pinned **build inputs**, not evidence that every
  installation of those major versions has been tested.
- OBS 31 macOS targets macOS 12+; OBS 32 macOS targets macOS 13+.
- Linux local user-plugin installs and system-wide CI `.deb` layouts are
  intentionally different; verify the one being distributed.
- **Do not install OBS 31 and OBS 32 variants together** in the same plugin
  location: both use the same module and data paths.
- The current milestone is an early `v0.1.0-preview.1` development preview.
  There is no published tagged release yet.

See [PACKAGING.md](../PACKAGING.md) for build/install commands and
[ARCHITECTURE.md](ARCHITECTURE.md) for behavior, storage, and ownership.
