# Documentation screenshots

These PNGs are actual Timeline and Macro Strip widgets, captured from the
test harness with real libobs, synthetic sources, the Qt Fusion style and
a fixed dark palette, or the actual Colorway Synthwave 84 theme. The frontend is mocked and the real English locale
is loaded for the capture. No personal OBS scenes are used; the themed capture uses only exported
Qt appearance from an isolated OBS profile.
They demonstrate the UI, not live-stream rendering or cross-platform
runtime acceptance.

From the repository root on Linux with the native OBS32 test build:

```sh
cmake --build build_x86_64 -j4
TIMELINE_REQUIRE_OBS_RUNTIME=1 \
  TIMELINE_DOC_SCREENSHOTS="$PWD/docs/screenshots" \
  QT_QPA_PLATFORM=xcb QT_STYLE_OVERRIDE=Fusion QT_SCALE_FACTOR=1 \
  LIBGL_ALWAYS_SOFTWARE=1 xvfb-run -a \
  build_x86_64/tests/run_tests testMacroOptionalBottomCaptionAndCompactXY
```

The absolute screenshot output directory is opt-in. Ordinary tests do not
write documentation assets or override the locale. Review screenshots
before committing; visual differences can depend on Qt and installed fonts.

- `timeline.png`: timed scene changes, a parameter ramp and a clock hold.
- `timeline-compact.png`: the compact layout with the same cues.
- `macro-strip.png`: compact default layout with six kinds of controls.
- `macro-strip-readouts.png`: optional bottom caption/readout row.
- `toggle-editor.png`: numeric preset assignment.
- `appearance-editor.png`: top label, lower row and XY size options.

## Colorway theme capture

The `synthwave84/` set uses the native OBS 32.2.0 resolved stylesheet,
palette, font and Qt resource search paths for Colorway Synthwave 84.
The theme was loaded by OBS in a disposable profile with only theme
assets copied from the installed theme directory.

The optional `TIMELINE_DOC_THEME` variable points to a local export
directory containing `theme.qss` (QApplication::styleSheet) and
`theme.qt` (QDataStream containing QPalette, QFont, theme search
paths and obs search paths, in order). Theme resources must still be
available at the exported search paths. Set this variable and change
the screenshot output directory to capture themed variants using the
same command above. Theme sources and exported machine-specific data
are not redistributed here. The root PNGs are the Fusion reference set.

The captures retain the existing native separator rendering, including
the new clock/zoom divider; theme-dependent contrast is unchanged.
