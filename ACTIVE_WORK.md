<!-- meristem-template:v1 -->
# Active Work — Timeline preview readiness

### Optional downstream-keyer scene control — 2026-11-12

- Added a new appended `DownstreamKeyerScene` event type, shared by Timeline and Macro Strip. Name a DSK, overlay scene and optional view/canvas; blank scene clears the DSK overlay, and a timed scene selection clears on event end.
- Uses OBS WebSocket's **in-process** `CallVendorRequest` for `downstream-keyer / dsk_select_scene` without a TCP client, credentials or any modifications to the DSK plugin.
- Added editor fields and English strings, persisted serialization support, icon and color, build-time API fallback, and explicit logs for missing/rejected requests.
- RED: new C++ regression failed to build because the event type was absent. GREEN: OBS32 native module/test executable built; strict Xvfb/libobs Qt test suite passed **51/51 tests, zero skips** after fixing the fixture's retained OBS scene-source registry state. New tests check vendor payload, timed hide, empty-name guard, editor values and both macro/timeline JSON round trips. Upstream DSK source confirms a blank scene clears via `SwitchToScene`.
- Scope: no real OBS installation, OBS profiles/scenes, or other plugin repositories changed. **Actual two-plugin runtime dispatch and visual composition remain unverified**; an isolated OBS session with Downstream Keyer installed is the next acceptance gate. OBS 33 and cross-platform DSK behavior have not been validated for this slice. The pre-existing main branch was four commits behind `origin/main`; no fetch/merge/push was performed.


## Outcome

Bring Timeline toward a verified first public preview with clean runtime packages, accurate documentation, and durable Meristem project context.

## Why it matters

Timeline is the maintainer's first public OBS plugin. Its runtime functionality and migration safety matter more than extra features or green compilation alone.

## Current state

### OBS 33 Linux compatibility checkpoint — 2026-10-30
- **Source/config:** Pinned Linux OBS 33.0.1 upstream tarball (SHA-256 verified), added explicit `obs33` compatibility selection, version guard, packaging label/dependency, and an Ubuntu 26.04 Linux CI matrix entry. Kept cross-platform artifacts and existing OBS31/OBS32 behavior unchanged.
- **Red/green:** OBS33 was previously rejected at CMake bootstrap. After changes an isolated Linux module compiled using real OBS33 Flatpak 33.0.0 SDK headers/libraries and passed relocation check in the Flatpak runtime. A mixed host/Flatpak Qt test executable *did not link* due Qt6.4/6.11, FFmpeg and glibc ABI differences; do not claim OBS33 runtime tests pass from this local probe.
- **OBS32 protection:** CMake native OBS32 rebuild and strict Xvfb Qt CTest passed on the same checkout; real OBS profile and installed module untouched.
- **CI repair and verification:** Cherry-picked previously green Ubuntu 24.04 runner fix from `90fdc27` as `8f36ea6`. OBS33 compatibility commit `0b67552` was pushed to `main`. Exact-head [Linux CI run 41154431438](https://github.com/myrqyry/timeline/actions/runs/41154431438) succeeded on all three targets (OBS31/32 Ubuntu24.04, OBS33 Ubuntu26.04); strict OBS33 Qt test logged **46 passed, 0 failed, 0 skipped**, with 3 residual allocations permitted by test cleanup. This verifies the pinned OBS33.0.1 Linux SDK build and headless suite, not interactive dock usage.
- **Still needed:** Isolated OBS33 live GUI module load/action/dock persistence smoke; full cross-platform OBS33 dependency pins/builds remain out of scope. Preserve user OBS32 install and settings.

- **Historical October 25 branch context:** `main` at `c490a40` included the documentation/substrate reconciliation after merged PR #3. Packaging hygiene, release documentation, Meristem adoption and the source-archive privacy fix (`de4f388`) are committed history.
- **Historical October 25 working tree:** Clean after pushing `c490a40`; the follow-up documented verified CI and maintainer's successful plugin-load report. See the October 30 checkpoint above for current code work.
- **Historical verification:** The six-platform/version build matrix and both formatting jobs passed for `c490a40`. Local strict Qt tests passed and the maintainer confirmed the installed plugin loaded. The separate Linux OBS31/OBS32 test workflow later failed on Ubuntu 26.04 teardown allocation accounting; a pinned-24.04 repair was independently CI-verified on `90fdc27` and incorporated above.

## Locked decisions

- Remove the obsolete pre-Meristem Hal Labs protocol boilerplate from root `AGENTS.md` rather than retaining it nostalgically.
- Preserve useful OBS-specific engineering rules and child instruction files.
- Meristem canonical project files are safe public project knowledge; the ignored `.meristem/local/` is private machine state.
- Preserve merged history, the normal OBS profile and existing config. The former draft follow-up PR #3 is now merged.

## Constraints / do-not-regress

- No undocumented change to runtime scheduling, transition lifetime, macro save/recovery, config paths or OBS SDK targeting.
- Do not expose credentials, raw conversations, local absolute workstation paths or personal continuity in canonical Git-tracked files.
- No unrequested merge, tag, public release or real OBS profile installation.

## Current objective

With Linux OBS31/32/33 exact-head CI green, complete isolated OBS33 live module/dock/action testing and remaining preview gates in `docs/RELEASE_CHECKLIST.md` before any release. Do not claim Windows/macOS OBS33 compatibility or a working user install from Linux CI alone.

## Verification

- Historical evidence recorded before this repair: PR #3 head `54040a4` passed [Linux tests](https://github.com/myrqyry/timeline/actions/runs/38859345025) and [six-platform matrix](https://github.com/myrqyry/timeline/actions/runs/38859345030).
- Local pre-Meristem cleanup: strict Xvfb 44 Qt tests, zero skipped; Debian package contained binary and locale but no `AGENTS.md`.
- Meristem initializer ran in **adopt** mode; doctor reported `ready` after semantic population. All six platform builds, strict OBS tests, and both format checks passed for `a265009`.
- Privacy regression RED: real CPack tarball included `.meristem/local/cpack-private-fixture.txt` despite `.gitignore`. GREEN: rebuilt tarball excludes the private directory and retains public `.meristem/PROJECT.md`. The synthetic fixture was removed after both runs.
- Documentation reconciliation (2026-10-25): local Ninja RelWithDebInfo configure/build with `TIMELINE_OBS_COMPATIBILITY=obs32` passed against installed libobs 32.0.1 and Qt 6.4.2. Strict Xvfb/software-GL CTest passed with 44 Qt tests, zero failures/skips. This uses the installed SDK, not the pinned CI OBS 32.2.1 build input. Meristem doctor reported `ready`; no package install or interactive OBS check was performed.
- Requested local installation (2026-10-25): the verified OBS32 binary and locale were installed atomically for OBS 32.2.0. Installed hashes match the staged CMake install, with no missing shared libraries. A rollback copy is retained under ignored `.meristem/local/`; existing saved data and legacy files were preserved. The running OBS process was not restarted, so loading the new binary remains unverified until restart.
- Post-install confirmation (2026-10-25): the maintainer reports that the installed plugin loaded successfully. This closes the pending local load check; it does not establish full editor/action coverage.
- Exact-head CI (2026-10-25): [build matrix and formatting](https://github.com/myrqyry/timeline/actions/runs/40243732211) passed for `c490a40`: Linux, Windows and macOS for both OBS31/OBS32, including the Ubuntu source-archive privacy checks. Release creation was skipped. The separate [Linux build/test workflow](https://github.com/myrqyry/timeline/actions/runs/40243732151) still reported both jobs in progress when checked.

## Open uncertainty

- Completion of the separate Linux build/test workflow for `c490a40` remains to be checked; the six-platform build/packaging matrix is green.
- Real Windows/macOS runtime loading, clean installation/upgrade and full interactive OBS action/editor coverage are not established. These remain in `docs/RELEASE_CHECKLIST.md`.
