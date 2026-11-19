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

## Recent local checkpoint (2026-11-19)

- Source checkpoint `9773946`: native OBS32 module build; strict Xvfb
  Qt/libobs suite **96 passed, 0 failed, 0 skipped**. Covers exact
  targeting, owner-aware hotkeys, ordered cue boundaries, Freeze Time,
  configurable ending states, numeric easing ramps, undo/redo, mixer
  bindings, text settings, stable caption layout and themed arc knobs.
- The matching CMake-staged binary and locale were installed in native
  OBS 32.2.0 on Linux. Staged and installed assets matched; relocation
  checks found no unresolved dependencies or symbols. Both docks and
  WebSocket vendor registered in an isolated OBS startup. Existing
  plugin configuration checksums were unchanged, and a rollback
  archive was retained locally.
- Historical real HyperShaderFX property smoke verified effect opacity
  and a shader-specific uniform through the binding layer, not rendered
  pixels. Ramp smoothness and the new controls' native interactive
  appearance are still unverified.
- Formatting: exact CI clang-format 19.1.1 checked the changed C++
  sources. No CMake source changes in this series.
- This verifies a local OBS32 source checkpoint, not the new remote
  CI matrix or OBS31/33, Windows or macOS runtime. No release was tagged.

## Before tagging (release blockers)

- [ ] Confirm the exact commit, its six build jobs, two Linux test jobs and
  formatting checks are green. Do not rely on an earlier green SHA.
- [ ] Inspect/download the **actual CI artifacts**: OBS 31/32 for Linux,
  Windows and macOS. Confirm they match the intended OBS major version.
- [ ] Inspect Linux `.deb` payloads and dependency fields, including the
  `obs-studio` major-version bounds; ensure no `AGENTS.md` or downloaded
  SDK/source trees are in the runtime packages.
- [ ] Confirm the source archive contains no `.deps` SDK downloads, no
  `.meristem/local/` private machine state, and no generated build/release
  outputs. CPack source exclusions must be verified independently from Git's
  ignore rules; Ubuntu CI injects a synthetic privacy fixture before packaging.
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
- [ ] Test compact mode toggle/reload, minimum dock height, and stacked lane
  scrolling. Resize Macro Strip with mixed controls, long labels and visible
  horizontal scrollbar under the active OBS theme.
- [ ] Verify undo/redo for exact-time edits, dragging and grouped operations;
  confirm it does not execute OBS actions and is disabled during playback.
- [ ] Save/restart/reload both timeline and Macro Strip state; import valid and
  invalid JSON. Corrupt imports must not erase current data.
- [ ] Verify Macro Strip migration from legacy Linux storage, scene-collection
  backup fallback, and protection of newer or unsaved edits.
- [ ] Trigger scene switch (default and named temporary transition), source
  show/hide (including nested items), filter enable/disable, media
  restart/stop/pause/resume/seek, OBS hotkey and nested
  timeline events against actual OBS sources.
- [ ] Bind sliders, arc knobs, toggles and text entries to actual source/filter properties. Check
  live discovery after rename/add/remove or shader change, relative/Shift
  drag, key/wheel routing, external readback and binding persistence. Verify
  missing targets cannot redirect writes. Verify stable caption geometry and
  light/dark theme changes without losing a text draft.
- [ ] Schedule numeric, boolean and text Set Parameter Value cues and trigger
  immediate preset buttons; verify no configuration-time writes, actual
  plugin behavior, and Macro Strip readback.
- [ ] Schedule native mixer volume/mute and numeric ramps with each easing
  curve; check actual rendered/audio output, endpoint, pause, seek, overlapping
  ramps, and updates to the same parameter. Profile costly source updates.
- [ ] Verify Freeze Time with short/long waits, manual pause, same-time cues,
  nested files and freeze at the loop/end boundary. Confirm subsequent timed
  cues retain visual positions.
- [ ] Verify explicit duration ending states (including leave unchanged and
  media/DSK alternatives), and owner-disambiguated hotkeys after renaming.
- [ ] Stop a parent timeline while a nested temporary transition is active:
  OBS must return to the original transition.
- [ ] Verify looping, pause/resume, seeking across duration boundaries and
  stopping during an active duration event. Check final cues at the end
  marker and chronological ordering after a delayed UI timer tick.
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
