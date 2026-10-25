<!-- meristem-template:v1 -->
# Active Work — Timeline preview readiness

## Outcome

Bring Timeline toward a verified first public preview with clean runtime packages, accurate documentation, and durable Meristem project context.

## Why it matters

Timeline is the maintainer's first public OBS plugin. Its runtime functionality and migration safety matter more than extra features or green compilation alone.

## Current state

- **Branch context:** Work continues on `main` from `00b801b`, the merge of PR #3 (`aster/pre-release-cleanup`). Packaging hygiene, release documentation, Meristem adoption and the source-archive privacy fix (`de4f388`) are committed history. The follow-up checkpoint reconciles developer documentation and project continuity.
- **Current working tree:** Clean at the start of substrate repair. Documentation/substrate reconciliation is the only intended change; no unrelated staged, unstaged or untracked implementation work was found. The ignored `.meristem/local/` directory was restored, and no production behavior was changed.
- **Verification state:** Earlier CI and isolated OBS-session evidence below is historical. Substrate doctor and local Linux configure/build/strict Qt tests passed during documentation reconciliation. The matching local user-plugin was installed and checked against staged CMake artifacts; interactive OBS verification was not repeated.

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

Continue first-preview readiness from merged `main`: establish exact-head source-packaging CI evidence for both Ubuntu OBS31/OBS32 paths and complete the remaining runtime validation in `docs/RELEASE_CHECKLIST.md` before release.

## Verification

- Historical evidence recorded before this repair: PR #3 head `54040a4` passed [Linux tests](https://github.com/myrqyry/timeline/actions/runs/38859345025) and [six-platform matrix](https://github.com/myrqyry/timeline/actions/runs/38859345030).
- Local pre-Meristem cleanup: strict Xvfb 44 Qt tests, zero skipped; Debian package contained binary and locale but no `AGENTS.md`.
- Meristem initializer ran in **adopt** mode; doctor reported `ready` after semantic population. All six platform builds, strict OBS tests, and both format checks passed for `a265009`.
- Privacy regression RED: real CPack tarball included `.meristem/local/cpack-private-fixture.txt` despite `.gitignore`. GREEN: rebuilt tarball excludes the private directory and retains public `.meristem/PROJECT.md`. The synthetic fixture was removed after both runs.
- Documentation reconciliation (2026-10-25): local Ninja RelWithDebInfo configure/build with `TIMELINE_OBS_COMPATIBILITY=obs32` passed against installed libobs 32.0.1 and Qt 6.4.2. Strict Xvfb/software-GL CTest passed with 44 Qt tests, zero failures/skips. This uses the installed SDK, not the pinned CI OBS 32.2.1 build input. Meristem doctor reported `ready`; no package install or interactive OBS check was performed.
- Requested local installation (2026-10-25): the verified OBS32 binary and locale were installed atomically for OBS 32.2.0. Installed hashes match the staged CMake install, with no missing shared libraries. A rollback copy is retained under ignored `.meristem/local/`; existing saved data and legacy files were preserved. The running OBS process was not restarted, so loading the new binary remains unverified until restart.

## Open uncertainty

- The source-archive privacy fix is committed and merged, but its exact-head CI results were not re-established during substrate repair. The prior local reproduction and correction remain historical evidence; cross-platform build/runtime status is a separate boundary.
- Real Windows/macOS runtime loading, clean installation/upgrade and full interactive OBS action/editor coverage are not established. These remain in `docs/RELEASE_CHECKLIST.md`.
