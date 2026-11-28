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

- Production source `f6ae128`: native OBS32 build and strict Xvfb
  Qt/libobs suite **101 passed, 0 failed, 0 skipped**. Covers exact
  targeting, ordered cues, Freeze Time, explicit endings, eased ramps,
  undo/redo, typed live controls, paired XY axes, customizable captions,
  type-first selection and two-state numeric/text toggles.
- The matching staged module and locale were installed in native OBS
  32.2.0 on Linux. Staged/installed assets matched; relocation checks
  found no unresolved dependencies or symbols. Both docks and the
  WebSocket vendor registered in isolated startup. Saved configuration
  checksums matched before/after; rollback archive retained locally.
- Reproducible headless screenshots capture the actual production widgets
  with synthetic sources and English locale. They were visually inspected
  before inclusion in the README/usage guide. The harness uses frontend
  test doubles, not the normal OBS profile.
- Historical real HyperShaderFX property smoke verified opacity and a
  shader-specific uniform through the binding layer, not rendered pixels.
  Ramp smoothness, native interactive acceptance and other platform
  runtimes remain separate verification gates.
- Exact CI clang-format checked the changed C++ files. Remote exact-head
  CI is not implied by local tests. No tag/release.

## OBS33 compatibility checkpoint (2026-11-19)

- [Exact-head Linux CI b7eb40b](https://github.com/myrqyry/timeline/actions/runs/44660008317):
  OBS31, OBS32 and OBS33 passed; OBS33 logged 102 passed, 0 failed/skipped.
- Native OBS33 Flatpak 33.0.1 with Qt 6.11.1: built the production module
  using its actual headers/libraries and matching moc, with the host
  compiler. Relocation check and isolated full OBS startup passed.
  Temporary test driver exercised a scheduled scene change then clicked
  a Macro Strip action to return. Both outcomes verified via real frontend
  current-scene readback. No production profiles or plugin installs changed.
- Corrected modern Linux user and system installation paths. The new
  default XDG data-directory installation loaded successfully; staged
  system module and locale paths verified, and local CPack Debian
  contents inspected (explicit dependency flags supplied to CPack because
  Flatpak libs have no host dpkg metadata). New Ubuntu26.04 package
  matrix and exact-head package job still need hosted verification.
- Strict native OBS32 suite after packaging changes: 102 passed.
  Windows/macOS OBS33 SDK and runtime support remain unverified.

## OBS33 Windows x64 preparation (2026-11-19)

- Official OBS 33.0.1 source ZIP downloaded and SHA-256 checked. Windows
  deps/Qt pins match the upstream 2026-10-23 release asset digests.
- Dedicated Visual Studio 18 2026 preset and 10.0.26100.0 SDK, isolated
  build/staging directories, matching hosted runner, and modern ProgramData
  plugin layout. Legacy OBS31/32 runner and archive layout retained.
- CI validates actual ZIP entries including locale and forbids a duplicate
  legacy DLL in OBS33. Local pin/preset/matrix contracts pass; Windows
  compilation, artifact contents and OBS runtime interaction remain pending.
- Previous Linux package workflow passed (run 44664678938).

## Hosted verification for `b8adbda`

- [Linux Build](https://github.com/myrqyry/timeline/actions/runs/45754355584)
  passed all three strict Linux jobs (OBS31/32/33).
- The [push run](https://github.com/myrqyry/timeline/actions/runs/45754355629)
  passed the Ubuntu OBS33 build and package checks. Clang-format and the
  OBS33 Windows x64 **Build Plugin** step failed; no OBS33 Windows artifact
  was uploaded. The artifact list was checked, but archives were not downloaded
  for independent inspection.

## Runtime-improvement checkpoint — 2026-11-27

- Code checkpoint `428f246` on `feat/timeline-runtime-improvements`: native
  OBS32 SDK build and strict Xvfb Qt/libobs suite, 127 passed, 0 failed, 0 skipped.
  See [the roadmap](../.meristem/TIMELINE_ROADMAP.md) for individual feature evidence.
- Candidate fixes for historical formatting and Windows OBS33 SDK failures
  are in `047f31f`, `e372395` and `8b5b7f2`. Hosted success is not established
  by those commits alone.
- The additive [shared CI workflow](../.github/workflows/shared-ci.yml) configures
  eight test-binary targets (OBS32.2.0/33.0.3 on Linux x86_64, Windows x64,
  macOS ARM64 and macOS Intel). It does not replace the legacy release matrix.
  Exact-candidate hosted results, actual downloads and native loads remain open.

## Before tagging (release blockers)

- [ ] Confirm formatting and the [Build Project](../.github/workflows/build-project.yaml)
  matrix are green for the exact candidate commit. The separate [Linux Build](../.github/workflows/linux-build.yml)
  workflow must pass strict runtime tests for OBS31/32 on Ubuntu 24.04 and
  OBS33 on Ubuntu 26.04. Also verify the additive shared qualification matrix
  and its downloadable test artifacts. Do not rely on an earlier green SHA.
- [ ] Inspect the **actual CI artifacts**: OBS31/32 for Linux x86_64, Windows
  x64 and macOS universal; OBS33 for Ubuntu 26.04 x86_64 and Windows x64.
  Check major labels and target-specific binary and locale paths against
  [PACKAGING.md](../PACKAGING.md). The OBS33 Windows ZIP must contain the
  modern DLL path and no legacy `bin/64bit` copy.
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
- [ ] Resolve preview tag naming and confirm release artifacts and checksums.
  `v0.1.0-preview.1` is the milestone label. The [draft-release workflow](../.github/workflows/push.yaml)
  accepts bare `MAJOR.MINOR.PATCH` or that version followed by `-beta` or
  `-rc` with optional digits; it does not accept the milestone label.
  No tag or publication until naming and the remaining checks are reviewed.

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
runtime check**. Run the OBS31 and OBS33 suites in their own build trees
with matching SDKs; see [PACKAGING.md](../PACKAGING.md).

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
- [ ] Test XY pads with two real source/filter parameters, including
  cross-source bindings, external readback, missing axes and row height.
- [ ] Schedule native mixer volume/mute and numeric ramps with each easing
  curve; check actual rendered/audio output, endpoint, pause, seek, overlapping
  ramps, and updates to the same parameter. Profile costly source updates.
- [ ] Record Macro Strip gestures into editable cues, confirm external
  readback is not captured, and verify one Undo removes the whole take.
- [ ] Verify a scheduled Data Source conditional cue with a fresh matching
  snapshot, then with stale/unshared data and with no provider. A skipped
  cue must leave subsequent ordinary cues running.
- [ ] Exercise named Macro Strip tabs, horizontal/vertical layout, slider orientation,
  grid assignment and action dispatch, spacing/reordering with edit-mode safety,
  and persistence across restarts in native OBS.
- [ ] Exercise pinned ruler, marquee/group movement, duration resizing, adaptive
  labels, fit zoom, muted cues, manual cue edges, target warnings and ripple edits
  in native OBS; verify undo, cancellation and reopening the saved document.
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

- OBS 31.1.2, 32.2.1 and 33.0.1 are pinned **build inputs**, not evidence that every
  installation of those major versions has been tested.
- OBS 31 macOS targets macOS 12+; OBS 32 macOS targets macOS 13+.
- Linux local user-plugin installs and system-wide CI `.deb` layouts are
  intentionally different; verify the one being distributed.
- **Do not install different OBS major variants together** in the same plugin
  location: both use the same module and data paths.
- The current milestone is an early `v0.1.0-preview.1` development preview.
  There is no published tagged release yet.

See [PACKAGING.md](../PACKAGING.md) for build/install commands and
[ARCHITECTURE.md](ARCHITECTURE.md) for behavior, storage, and ownership.
