# Documentation screenshots

These PNGs are actual Timeline and Macro Strip widgets, captured from the
test harness with real libobs, synthetic sources, the Qt Fusion style and
a fixed dark palette. The frontend is mocked and the real English locale
is loaded for the capture. No personal OBS profile or scenes are read.
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
- `macro-strip.png`: compact default layout with six kinds of controls.
- `macro-strip-readouts.png`: optional bottom caption/readout row.
- `toggle-editor.png`: numeric preset assignment.
- `appearance-editor.png`: top label, lower row and XY size options.
