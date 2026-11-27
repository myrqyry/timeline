# Shared CI qualification

The additive Linux OBS32.2.0/33.0.3 pilot uses shared obs-plugin-ci
238f06a. It retains the OBS31/32/33 native and release workflows. All
105 Qt test functions must execute with real OBS runtime required;
mock-only or skipped results fail. The caller fetches the pinned real
upstream websocket vendor header and stages version-specific module,
locale and license bytes. Downloadable binary test artifacts are enabled; no automatic release.
Local existing OBS32.2.0 build: all 105 Qt functions passed, staged
three-file archive passed checksums, module exports and architecture.
Hosted SDK and OBS33 remain unverified for the new caller.

Full matrix extension: pin e9eb704 uses the shared native SDK driver for
Windows x64, macOS ARM64 and macOS Intel on OBS32.2.0 and 33.0.3.
The independent native packaging workflows already build with
TIMELINE_BUILD_TESTS=OFF; the new native legs follow that same contract.
Linux retains all 105 Qt runtime tests. Native package layout fixtures
passed on Linux for all six combinations; this is not hosted proof.
