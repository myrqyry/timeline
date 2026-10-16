<!-- meristem-template:v1 -->
# Active Work — Timeline preview readiness

## Outcome

Bring Timeline toward a verified first public preview with clean runtime packages, accurate documentation, and durable Meristem project context.

## Why it matters

Timeline is the maintainer's first public OBS plugin. Its runtime functionality and migration safety matter more than extra features or green compilation alone.

## Current state

- **Base branch:** PR #2 was merged into `main` at `c4b51cb`. Its verified head was `33a15f5`.
- **Branch context:** draft PR #3 (`aster/pre-release-cleanup`) is based on that merge. Its pre-Meristem head `54040a4` contains packaging hygiene, release documentation, and the CI gersemi fix. All six platform/version build jobs, both Linux test jobs and both formatting checks completed successfully on that commit.
- **Meristem adoption:** Root `AGENTS.md` now routes to real Meristem state instead of obsolete pre-Meristem protocols; scoped project files, effective ignore rules and an active-work handoff are on the PR branch. Head `a265009` passed Linux OBS31/OBS32 tests and the full six-platform matrix.
- **Source archive privacy follow-up:** A PR #3 review correctly identified that CPack includes `.meristem/local/` despite Git ignoring it. A synthetic-file reproduction leaked into an actual source tarball. The proposed fix adds a CPack source exclusion and a CI canary check before source artifact upload. Check the latest commit/CI for verification.
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

Validate source packaging confidentiality on both Ubuntu OBS31/OBS32 CI paths, reply to the review, and preserve the draft PR without merging or releasing. Continue preview runtime validation separately.

## Verification

- Before Meristem edits, draft PR #3 head `54040a4` passed [Linux tests](https://github.com/myrqyry/timeline/actions/runs/38859345025) and [six-platform matrix](https://github.com/myrqyry/timeline/actions/runs/38859345030).
- Local pre-Meristem cleanup: strict Xvfb 44 Qt tests, zero skipped; Debian package contained binary and locale but no `AGENTS.md`.
- Meristem initializer ran in **adopt** mode; doctor reported `ready` after semantic population. All six platform builds, strict OBS tests, and both format checks passed for `a265009`.
- Privacy regression RED: real CPack tarball included `.meristem/local/cpack-private-fixture.txt` despite `.gitignore`. GREEN: rebuilt tarball excludes the private directory and retains public `.meristem/PROJECT.md`. The synthetic fixture was removed after both runs.

## Open uncertainty

- The source-archive privacy fix needs its own exact-head CI results after pushing. CPack source behavior was locally reproduced and corrected; cross-platform build/runtime status is a separate boundary.
- Real Windows/macOS runtime loading, clean installation/upgrade and full interactive OBS action/editor coverage are not established. These remain in `docs/RELEASE_CHECKLIST.md`.
