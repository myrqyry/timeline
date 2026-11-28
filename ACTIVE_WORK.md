<!-- meristem-template:v1 -->
# Active Work — Timeline runtime improvements and preview readiness

## Outcome

Implement the accepted 100-item improvement program in [the roadmap](.meristem/TIMELINE_ROADMAP.md), preserve verified checkpoints, and qualify a specific candidate against [the release checklist](docs/RELEASE_CHECKLIST.md).

## Current state — 2026-11-27

- Branch `feat/timeline-runtime-improvements` at code checkpoint `428f246`, based on shared CI checkpoint `f7503ce`. Local `main` and local tracking ref `origin/main` were both `b8adbda` when inspected; no new remote fetch is implied.
- Roadmap: 17 implemented, 6 partial, 77 remaining. The nine implementation batches are recorded in the roadmap and Git history.
- Current code checkpoint built with the matching local OBS32 SDK; strict Xvfb Qt/libobs suite: 127 passed, 0 failed, 0 skipped (39.22 s). This is not interactive OBS or hosted cross-platform qualification.
- Earlier documentation reconciliation has been reviewed and updated to reflect the current branch. Historical handoff remains available via `git show b8adbda:ACTIVE_WORK.md`; machine-specific evidence remains under ignored `.meristem/local/`.

- Follow-up interaction polish: shared paint-only motion for cue and macro action feedback and passive knob/XY readback; direct input and OBS execution remain exact. Current strict suite: 131 passed, zero failures/skips (40.61 s). No change to numbered roadmap counts. Native frame-time profiling remains open.

## Locked decisions and constraints

- The timeline and scheduled cue sequencing remain central. Keep exact targets fail-closed, edits undoable, and playback clocks independent of visual refresh.
- Recording inserts editable cues as one take; conditional Data Source cues read a fresh opt-in snapshot at dispatch.
- Build separately for matching OBS majors and preserve the maintainer's normal profile, plugin installation and configuration. Installation, push, merge, tagging and release are distinct actions.

## Next work

- Continue the accepted roadmap: named bookmarks (#9), timed cue audition (#6 remaining), target-health coverage (#7 remaining), and playback lifecycle.
- Before release, obtain exact-candidate CI and artifact evidence for legacy and shared workflows. The historical Windows OBS33 SDK and formatting failures have local candidate fixes (`047f31f`, `e372395`, `8b5b7f2`); do not claim hosted resolution without a matching rerun.
- Complete native interactive and clean upgrade gates, and settle release tag naming before publication.

## Historical evidence and open uncertainty

- `0a74550`: isolated OBS32.2.0 recorded a real Color Source Width ramp. `b8adbda`: 105 strict Qt/libobs passes; isolated Data Source condition probe verified matching and safe skipping. Hosted Linux jobs passed, with formatting and Windows OBS33 failures on the push run.
- Earlier OBS33 Flatpak native scene/action smoke and package evidence are recorded in the release checklist. Shared CI extends the matrix with OBS32.2.0/33.0.3 Linux, Windows x64, macOS ARM64 and macOS Intel; the current exact candidate is not hosted-qualified.
- The installed normal-profile plugin version was not rechecked. Current changes have not been installed or pushed in the roadmap implementation sequence. Full native interactive playback, cross-platform loading, performance and upgrade gates remain open.
- The draft-release workflow does not accept the milestone label `v0.1.0-preview.1` as a tag.
