<!-- meristem-template:v1 -->
# Learnings

## 2026-10-16 — Inspect SDK headers before accepting API review findings

**Signal:** Automated review called `obs_module_config_path(file)` an undefined symbol.

**Mechanism:** `obs-module.h` defines it as a macro expanding to `obs_module_get_config_path(obs_current_module(), file)`; repo-only symbol searches do not discover installed SDK declarations.

**Scope:** OBS plugin review.

**Evidence:** Header and Linux binary symbol inspection, real OBS32 loading, passing OBS31/32 and cross-platform builds on `33a15f5`.

**Recommended behavior:** Verify APIs and linkage against the installed SDK before making review-driven changes.

**Validation status:** tested.

## 2026-10-16 — CI needs actual release-mode packaging tests

**Signal:** Ordinary PR builds passed while explicit Debian package testing exposed missing maintainer metadata and pinned SDK `dpkg-shlibdeps` lookup failures. Installed data had included `AGENTS.md`.

**Mechanism:** SDK libraries built on CI lack system Debian metadata; generic data installation includes development files unless filtered.

**Scope:** Linux package pipeline and cross-platform resource packaging.

**Evidence:** `cmake/linux/defaults.cmake`, `.github/workflows/build-project.yaml`, successful PR #2 release checks and PR #3 package inspection.

**Recommended behavior:** Exercise release packaging on PR CI, verify version-bounded OBS dependencies, and inspect actual payloads for stray development files.

**Validation status:** tested for Debian; sampled Windows/macOS artifact payloads verified.

## 2026-10-16 — Match the CI formatter rather than trusting unpinned local output

**Signal:** Local gersemi passed while CI gersemi 0.21.0 rejected formatting in `cmake/macos/helpers.cmake`.

**Mechanism:** Newer formatter and CI version expand a `cmake_path` argument differently.

**Scope:** CMake formatting.

**Evidence:** PR #3 failed job logs followed by corrected `54040a4` and green exact-head CI.

**Recommended behavior:** Use the CI version when formatting checks disagree.

**Validation status:** tested.
