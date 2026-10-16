<!-- meristem-template:v1 -->
# Active Work — Timeline preview readiness

## Outcome

Bring Timeline toward a verified first public preview with clean runtime packages, accurate documentation, and durable Meristem project context.

## Why it matters

Timeline is the maintainer's first public OBS plugin. Its runtime functionality and migration safety matter more than extra features or green compilation alone.

## Current state

- **Base branch:** PR #2 was merged into `main` at `c4b51cb`. Its verified head was `33a15f5`.
- **Branch context:** draft PR #3 (`aster/pre-release-cleanup`) is based on that merge. Its pre-Meristem head `54040a4` contains packaging hygiene, release documentation, and the CI gersemi fix. All six platform/version build jobs, both Linux test jobs and both formatting checks completed successfully on that commit.
- **Meristem adoption:** Root `AGENTS.md` now routes to real Meristem state instead of obsolete pre-Meristem protocols; scoped project files, effective ignore rules and an active-work handoff are included in this follow-up branch. Check live Git status to distinguish committed branch state from any uncommitted edits; repeat CI for the exact final head.
- No Timeline scheduling/runtime source refactor is part of this adoption; runtime behavior is unchanged.

## Locked decisions

- Remove the obsolete pre-Meristem Hal Labs protocol boilerplate from root `AGENTS.md` rather than retaining it nostalgically.
- Preserve useful OBS-specific engineering rules and child instruction files.
- Meristem canonical project files are safe public project knowledge; the ignored `.meristem/local/` is private machine state.
- Keep the work in a draft follow-up PR and preserve merged history, normal OBS profile and existing config.

## Constraints / do-not-regress

- No undocumented change to runtime scheduling, transition lifetime, macro save/recovery, config paths or OBS SDK targeting.
- Do not expose credentials, raw conversations, local absolute workstation paths or personal continuity in canonical Git-tracked files.
- No unrequested merge, tag, public release or real OBS profile installation.

## Current objective

Verify Meristem adoption via `meristem_init.py doctor --root .`, the effective Git ignore/trackability and project context consistency. Maintain the draft follow-up PR and verify its exact-head CI after commits. Do not merge or publish as a side effect.

## Verification

- Before Meristem edits, draft PR #3 head `54040a4` passed [Linux tests](https://github.com/myrqyry/timeline/actions/runs/38859345025) and [six-platform matrix](https://github.com/myrqyry/timeline/actions/runs/38859345030).
- Local pre-Meristem cleanup: strict Xvfb 44 Qt tests, zero skipped; Debian package contained binary and locale but no `AGENTS.md`.
- Meristem initializer ran in **adopt** mode and doctor reported structural `ready` before semantic population; repeat after editing.

## Open uncertainty

- The Meristem-init commit's CI results remain to be checked after pushing.
- Real Windows/macOS runtime loading, clean installation/upgrade and full interactive OBS action/editor coverage are not established. These remain in `docs/RELEASE_CHECKLIST.md`.
