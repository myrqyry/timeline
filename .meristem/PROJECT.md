<!-- meristem-template:v1 -->
# Timeline for OBS — Project

## Purpose

Timeline is an OBS Studio dock plugin for composing and running scheduled OBS actions visually, with a separate Macro Strip dock for immediate reusable actions and live source/filter parameter controls. The goal is a reliable publicly usable plugin, not merely a compilable editor.

## Invariants

- Keep the visual timeline and precise scheduled cue sequencing central. Supported actions include scene switching, source show/hide, media restart/stop/pause/resume/seek, filter enable/disable, hotkeys, nested playback, defined scalar parameter values, numeric eased ramps, Freeze Time clock holds, explicit ending states and the optional Downstream Keyer integration. Serialized `MediaSeek` (ID 3) remains reserved.
- New explicit targets use stable source/filter, scene-item or hotkey registerer identity; do not silently substitute a different target. Parameter controls and scheduled values share the live OBS property catalog and typed settings-update path.
- Preserve duration boundaries, playback/loop behavior and nested timeline cancellation. Nested temporary transitions unwind child-first; nested timeline cycles must not run indefinitely.
- Invalid imports cannot silently destroy working state. Macro Strip saves atomically to writable OBS module configuration, retains unsaved in-memory edits on failed writes, and treats scene-collection copies as recovery backups rather than authority over newer edits.
- OBS31/OBS32 artifacts are SDK/ABI-specific. Passing mocks/builds/packaging is not the same as testing real OBS user interactions or Windows/macOS runtime loading.
- Keep the maintainer's normal OBS profile, scene collections, plugins and stream configuration untouched in testing. Merging, tagging and releasing remain separate actions.

## Architecture and authority

- `src/plugin-main.cpp` registers the OBS module and both docks.
- `src/timeline-model.cpp` owns event data; `src/timeline-scheduler.cpp` dispatches timing; `src/obs-executor.cpp` performs OBS actions.
- `src/headless-timeline.cpp` runs nested timelines. `src/timeline-dock.cpp` and `src/event-editor.cpp` implement editing. `src/macro-strip-dock.cpp` owns the immediate-action UI and state lifecycle. `src/*serialization.cpp` handles JSON.
- `data/locale/` holds runtime assets. `data/AGENTS.md` is source-only guidance, excluded from installed packages.
- Refer to `README.md` for user-facing capabilities, `docs/ARCHITECTURE.md` for architecture and behavior, `PACKAGING.md` for artifact layout, and `docs/RELEASE_CHECKLIST.md` for release gates. Local `src/AGENTS.md`, `tests/AGENTS.md` and `data/AGENTS.md` govern their own areas.

## Known-good workflows

- Select `TIMELINE_OBS_COMPATIBILITY=obs31` or `obs32` using matching SDK/build trees. Linux preset: `cmake --preset ubuntu-x86_64 -DTIMELINE_OBS_COMPATIBILITY=obs32`, then `cmake --build --preset ubuntu-x86_64`.
- Run Qt regressions with Xvfb, software OpenGL and `TIMELINE_REQUIRE_OBS_RUNTIME=1`. Without that strict flag, missing OBS/OpenGL startup may skip the suite. Exact commands: `docs/RELEASE_CHECKLIST.md`.
- Validate code with `.clang-format` and `.gersemirc`, CI jobs, and isolated real OBS smoke checks as appropriate. Cross-platform CI is not a substitute for runtime installation.

## Verification baseline

- November 19, 2026: production source `f6ae128` passed all 101 strict native Qt/libobs tests and installed successfully in OBS32.2.0. Both docks/WebSocket vendor registered under isolated startup; configuration checksums unchanged. Current headless widget screenshots and repeatable capture instructions included. Native interactive acceptance, ramp performance and other platform runtimes remain unverified.

- Before Meristem adoption, draft PR #3 head `54040a4` passed Linux OBS31/OBS32 test jobs and the six Linux/Windows/macOS OBS31/OBS32 build matrix, including formatting. That **does not validate later Meristem changes**.
- The Linux strict Qt suite previously ran 44 tests with no failures/skips. Isolated real OBS32 Linux smoke demonstrated source hide/restore, scene switching, Macro Strip migration and canonical-backup precedence.
- CI package inspections confirmed the runtime asset exclusion on Linux, Windows and macOS OBS31; the completed matrix established macOS OBS32 compilation/packaging. Actual Windows/macOS plugin runtime use remains untested.

## Open structural uncertainty

- Full interactive editing and event execution in real OBS, clean upgrades and Windows/macOS runtime installation remain release gates, not supported claims.
- No tagged public release was established at initialization. `ACTIVE_WORK.md` tracks preview readiness from merged `main` and distinguishes historical verification from current-tree evidence.
