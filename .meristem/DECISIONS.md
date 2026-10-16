<!-- meristem-template:v1 -->
# Decisions

## 2026-10-16 — OBS-version-specific release gates

**Decision:** Build OBS 31 and 32 against separate SDKs and distinctly labeled packages; distinguish CI build verification from installed, interactive runtime verification.

**Why:** ABI and packaging can differ across versions/OSes. Shipping based on compilation alone invites unsupported claims.

**Evidence:** `buildspec.json`, `PACKAGING.md`, `docs/RELEASE_CHECKLIST.md`, PR #2 and draft PR #3 CI.

## 2026-10-16 — Macro Strip authoritative writable state

**Decision:** Use OBS module config for macro buttons. Preserve legacy imports and treat scene-collection copies as recovery backups, not overrides of valid canonical or unsaved in-memory state.

**Why:** Installed data can be read-only; failed autosave and stale backups previously risked discarding edits.

**Evidence:** `src/plugin-main.cpp`, `src/macro-strip-dock.cpp`, `tests/main_test.cpp`.

## 2026-10-16 — Preserve instructions, exclude them from runtime packages

**Decision:** Keep source-local `AGENTS.md` guidance but exclude it from plugin installation assets. Replace obsolete pre-Meristem agent boilerplate with bounded Meristem project continuity, without keeping dead protocols for historical reasons.

**Why:** Agents need authoritative active context; users installing OBS plugins do not need agent instructions. Git history preserves provenance.

**Evidence:** Draft PR #3, `CMakeLists.txt`, `cmake/macos/helpers.cmake`, `AGENTS.md`.
