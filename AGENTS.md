# Timeline for OBS — Agent Instructions

Timeline is a C++17, Qt6 and libobs plugin that adds a visual scheduler and a Macro Strip dock. Prefer verified, usable behavior to scaffolding or feature claims.

## Project authority

- `.meristem/PROJECT.md` — stable purpose, architecture boundaries and invariants.
- `ACTIVE_WORK.md` — current objective, PR, verification and remaining uncertainty.
- `README.md` — user-facing features and installation.
- `docs/ARCHITECTURE.md` — detailed runtime, data model and ownership.
- `docs/RELEASE_CHECKLIST.md` — release evidence and unverified gates.
- `PACKAGING.md` — builds and distributable installation layouts.
- Area-specific guidance: `src/AGENTS.md`, `tests/AGENTS.md`, `data/AGENTS.md`.

Source, runtime and tool evidence outrank stale prose. Read only the relevant project/area guidance; don't automatically load every state file or invent missing continuity documents.

## Engineering and safety

- Change working code narrowly. For behavioral changes, reproduce the issue with a failing regression test, fix it, and run relevant/full tests.
- Compile against the selected OBS31/OBS32 SDK. Keep packages separately labeled and don't install incompatible majors together.
- Build success and test doubles do **not** prove real OBS behavior. Test with isolated OBS profiles; never silently modify the developer's ordinary scenes, plugin installation, or stream setup.
- Preserve original config on invalid imports, failed saves and migrations. Writable configuration belongs in OBS module config, not installed runtime data.
- OBS source references and allocator ownership must be balanced. Queue UI changes from OBS hotkey/frontend callbacks to the owning Qt thread; do not block graphics/render paths.
- Confirm SDK macro/function declarations against the actual OBS headers rather than trusting a repo-only search or automated review.
- No unrequested merges, releases, tags, destructive resets or publication. Keep commits focused and recoverable.

## OBS entry and conventions

- Keep `OBS_DECLARE_MODULE()` and `OBS_MODULE_USE_DEFAULT_LOCALE("timeline", "en-US")` in `src/plugin-main.cpp`.
- Production sources are listed in `CMakeLists.txt`; version metadata originates in `buildspec.json`.
- Follow the checked-in `.clang-format` (tabs, width 8, line limit 120) and `.gersemirc`. Check the exact CI formatter versions if local and CI differ.
- Log to OBS with `blog(LOG_*, ...)`.
- Configure a matching major with `cmake --preset ubuntu-x86_64 -DTIMELINE_OBS_COMPATIBILITY=obs32`; build with `cmake --build --preset ubuntu-x86_64`. For strict Xvfb tests use the commands in `docs/RELEASE_CHECKLIST.md`; a skipped test isn't a passing runtime check.

## Task routing

Read `src/AGENTS.md` for dock UI, event timing, OBS actions or rendering. Read `tests/AGENTS.md` for fixtures, Qt Test, frontend test doubles and OBS lifecycle. Read `data/AGENTS.md` when changing localization or bundled assets. Development guidance must not be included in installed plugin files.

<!-- meristem:start -->
## Meristem project continuity

This repository uses Meristem project state.

Before non-trivial work:

1. Treat current repository/runtime evidence as the authority for what exists now.
2. Read `.meristem/PROJECT.md` for durable project purpose, invariants, and architecture boundaries when relevant.
3. If `ACTIVE_WORK.md` exists, read it for the current objective, locked decisions, constraints, verification state, and unresolved uncertainty.
4. Consult `.meristem/DECISIONS.md`, `LEARNINGS.md`, `ERRORS.md`, or `FEATURE_REQUESTS.md` only when they can materially change the current task.
5. Do not promote session detail into durable files merely because it is recent. Persist consequential state before dependent continuation when losing it would make later work expensive or misleading.
6. Preserve existing project conventions and working state. Do not broadly rewrite configuration or clean up unrelated code for convenience.
7. Verify user-visible outcomes before claiming completion.
8. Treat canonical `.meristem/*.md` files and `ACTIVE_WORK.md` as commit-safe repository knowledge. Keep secrets, personal continuity, raw conversations, and machine-specific state out of them; use `.meristem/local/` only for repo-local private/machine state.

Use the narrowest capable tool or specialist. Current source and runtime truth outrank stale continuation notes.
<!-- meristem:end -->
