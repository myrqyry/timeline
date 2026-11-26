<!-- meristem-template:v1 -->
# Errors

## 2026-10-16 — Debian packaging against a pinned but uninstalled OBS SDK

**Failure:** `dpkg-shlibdeps` could not resolve `libobs.so.30` and `libobs-frontend-api.so.30` during release packaging despite successful compilation.

**Cause:** CI builds the pinned SDK but does not install it as system Debian packages.

**Correction:** Use explicit, OBS-major-bounded `obs-studio` dependency metadata for pinned-SDK builds; retain system dependency scanning for installed system libraries. Exercise `package` in PR CI.

**Evidence:** `cmake/linux/defaults.cmake` and successful PR #2 build matrix at `33a15f5`.

## 2026-10-16 — Pre-release package included agent instructions

**Failure:** A Linux Debian payload contained `usr/share/obs/obs-plugins/timeline/AGENTS.md`.

**Cause:** `install(DIRECTORY data/ ...)` copied every file, including source-only instructions.

**Correction:** Explicitly exclude `AGENTS.md` on Linux/Windows and in macOS bundle resources; enforce via PR CI and artifact inspection.

**Evidence:** Draft PR #3 and its completed `54040a4` checks.

## 2026-10-16 — CI gersemi mismatch

**Failure:** An initial PR #3 check failed CMake formatting despite local success.

**Cause:** Local formatter emitted syntax formatting different from CI gersemi 0.21.0.

**Correction:** Reformat using CI gersemi 0.21.0, retest and push `54040a4`.

**Evidence:** PR #3 CI history.

## 2026-11-26 — OBS33 Windows SDK configure/build and formatter

Run 45754355629 passed seven of eight configured platform/major builds.
clang-format 19.1.1 rejected two source files; exact formatter confirmed
the failures and corrected whitespace only. Windows OBS33 downloaded and
configured the pinned OBS 33.0.1 source successfully but failed building
the Debug OBS SDK. Child stdout had been suppressed; underlying compiler
diagnostic is unknown. Candidate repair limits optimized Windows OBS33
CI builds to the Release SDK, with existing optimized imported-config
mapping, explicit browser/scripting disablement and visible diagnostics.
Explicit Debug and all other platform SDK configuration choices retained.
SDK configuration regression and existing readiness contract passed,
gersemi 0.21.0 passed, local strict OBS32 suite 105/0/0. Native Windows
qualification remains pending; do not treat this as a proven compiler fix.
