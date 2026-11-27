# Shared CI qualification

The additive Linux OBS32.2.0/33.0.3 pilot uses shared obs-plugin-ci
238f06a. It retains the OBS31/32/33 native and release workflows. All
105 Qt test functions must execute with real OBS runtime required;
mock-only or skipped results fail. The caller fetches the pinned real
upstream websocket vendor header and stages version-specific module,
locale and license bytes. No package upload or release during qualification.
Local existing OBS32.2.0 build: all 105 Qt functions passed, staged
three-file archive passed checksums, module exports and architecture.
Hosted SDK and OBS33 remain unverified for the new caller.
