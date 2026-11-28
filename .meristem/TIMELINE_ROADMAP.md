# Timeline improvement program — 2026-11-27

Source: user-supplied "Architectural & Product Strategy Analysis: Timeline for OBS", 100 distinct enhancements. User requested implementation of all code fixes and improvements. This is the accepted work queue, not evidence that all features are absent or delivered. Reconcile each item with current source before changing it. Existing release gates remain in force; do not claim cross-platform or interactive OBS verification from Qt/libobs fixture tests.

## Checkpoints and status

- e28ebe3: #24, #51, #52, #85; #88 partial (protect unreadable startup file; dirty shutdown optimization remains).
- 5ae401b: #15, #41, #86; #62 partial (event ID, label, duration and nested flag).
- UI follow-up: #56, #10 partial (cursor anchor, not fit), #60 partial (ruler hit radius shared; narrow body hit zone and handle geometry remain). See git history and test results for verification.

- Fourth implementation batch: #1 and #53 implemented, #55 partial. OBS 32 Linux build, focused regressions (including cancellation and duplicate text/preset bindings) and strict full Qt/libobs suite passed. #54 signal-driven readback and #58 cross-poll schema cache remain open.

- Fifth implementation batch: #2 and remaining #10 implemented. Long cues extend the scrollable canvas; overview scale uses adaptive ruler ticks. Focused viewport gesture, clamp, undo, cancel and zoom tests plus strict OBS 32 Linux Qt/libobs suite passed (35.92s). Sticky ruler #3 and visual ramp curves #4 remain open.

- Sixth implementation batch: #3 and #4 implemented. Pinned ruler handles horizontal and vertical scroll, hit priority and repaint; ramp direction/easing uses cached samples of the actual playback interpolation. Both new regressions failed before implementation, then passed along with strict OBS 32 Linux Qt/libobs suite (35.12s). Very short ramps keep the standard duration line and grip; interactive OBS rendering and cross-platform verification remain open.

- Seventh implementation batch: #5 implemented, #6 partial. Actual context-menu actions exercised against a real libobs filter; mute round-trip/undo, scheduler bypass of point/duration/freeze/parameter cues, seek behavior and orphan-ending regression verified. Strict OBS 32 Linux Qt/libobs suite passed (37.56s). Manual duration actions explicitly fire individual edges; ramp end samples its endpoint. No automatic timed audition or standalone freeze action. UI blocks these actions during playback/recording and inside paused duration spans. Existing seek/stop semantics are unchanged; no claim of global state rollback. Older builds ignore the new mute field.

- Eighth implementation batch: #72 implemented, #7 partial. Read-only target validation reuses explicit UUID filter/media/scene-item resolution; visible paused cues are checked in rotating batches of four per 750ms. Missing exact filter removal and reattachment, duration rendering at different zooms and resize regressions verified. Strict OBS 32 Linux Qt/libobs suite passed (38.96s). External vendor and nested timeline target validation, transition overrides, signal-driven invalidation and live playback health feedback remain open.

- Ninth implementation batch: #8 implemented. Insert time shifts cues at/after the start; remove time deletes cues starting in the half-open interval and shifts later cues. Cue payloads/durations and playhead/end marker are preserved; cuts beginning inside duration spans or violating fixed end limits are rejected atomically. Disabled during active playback, recording and dragging. Context-menu regression failed before implementation and passed afterward; boundary, undo/redo, no-op and invalid-range checks passed. Strict OBS 32 Linux Qt/libobs suite passed (39.22s). No automatic end-marker extension, span splitting, or live OBS state rollback.

- Interaction polish (user-requested, outside numbered feature count): shared lazy paint-only motion for cue and Macro Strip buttons, passive knob/XY readback, immediate direct input, style duration capped at 120 ms. Compact viewport clearance now uses the same visual padding as cue bounds. Focused regressions reproduced abrupt transitions; full strict OBS32 Qt/libobs suite passed (131 passed, 0 failed/skipped, 40.61 s). Native frame pacing and third-party callback costs remain unmeasured.

- Macro Strip layout batch: #32 and #33 implemented; additional 2×2 circular action grids and per-strip/per-slider orientation. Version 2 bank documents validate all banks before replacement; legacy single-strip files still load. Model stores spacing rather than spacer widgets. Edit mode disables live controls, previews gaps without model changes, commits on release, supports Shift spacing-only drag and Escape cancellation. Regression tests reproduce missing bank/layout behavior, verify parameter execution from grid cells, orientation, tab switching, legacy/multi-strip round trip, malformed document preservation, spacing/reorder and cancellation. Strict OBS32 Qt/libobs suite: 133 passed, 0 failed/skipped (40.82s). Native OBS verification, simultaneous independent docks, unrestricted 2D arrangement, animated reordering and layout undo remain open.

- Individual Macro Strip docks and layout polish: #32/#33 extended, #35 partial. Independent docks register with stable hashed IDs before OBS restores layout; one canonical workspace document validates tabs and all individual strips, forwards direct parameter recording, and preserves hidden dock definitions. Menu-driven creation, moving a bank to a dock and returning it without duplication, registration identity, real parameter actions, invalid-document preservation, layout history and bounded visible-control reorder animation tested. Theme-padding regression reproduced and corrected short-label clipping. Strict OBS32 Qt/libobs: 136 passed, zero failures/skips (41.15s). Real isolated OBS 32.2.0 probe passed 15 first-run and 5 restart checks, including actual mixer writes, scene-switch actions, dock placement and visibility restore, layout edit/undo/redo and saved spacing. Captured and inspected native dock screenshots; full labels fit. Native evidence: /tmp/timeline-macro-native.ewxuE8; reproducible probe tests/native/run-macro-layout.sh. Unrestricted 2D arrangement, general macro undo/duplication, native streaming-load profiling and Windows/macOS acceptance remain open. Normal profile/installation not touched.

All unmarked items remain open or require reconciliation. Implementation order: hot-path safety and efficiency, direct manipulation, shared typed target/control model, interoperability and creative extensions, then distribution and coverage. Do not introduce new work on the audio/video callback thread; preserve fail-closed targeting, undo semantics, load/save safety, and isolated test configurations.

## 1–10: Core editing

1. Direct duration-handle resizing (implemented: snap, one undo, Escape cancels)
2. Rubber-band selection and group dragging (implemented: modifier selection, shared snapped delta, one undo, Escape cancels)
3. Viewport-pinned time ruler (implemented: stays visible through vertical scrolling, horizontal time alignment, marker scrubbing)
4. Visual ramp curve and direction (implemented: shared playback easing, bounded normalized samples; short spans retain line and grip)
5. Per-cue mute/bypass (implemented: persistent, undoable, visibly dimmed; skips start/end/hold/ramp, preserves ending action of an already-started span)
6. Single-cue test/run now (partial: manual point/start/end execution while paused; full timed audition and independent cancellation remain open)
7. Broken-target health badges (partial: visible paused cues, scene/source/filter/media/parameter/hotkey checks; external/nested targets and transition overrides not validated)
8. Ripple insert/delete (implemented: ruler/empty-space menu, snapped start, explicit interval, atomic undo, fixed-end and crossing-span safeguards)
9. Named ruler markers/bookmarks
10. Cursor-anchored zoom and zoom-to-fit (implemented: fit includes duration endpoints and end marker; content-aware horizontal extent)

## 11–20: Documents and show management

11. Managed timeline library and switcher
12. Scene-collection scoping
13. Portable nested references
14. Active nested monitor
15. Nested WebSocket vendor propagation (implemented)
16. Pin to Macro Strip
17. Portable bundle export
18. Playback speed
19. A/B loop region
20. JSON drag-and-drop load/nesting

## 21–30: Transport and scheduling

21. Manual hold until go
22. Live playback overdub
23. RDP curve simplification
24. Indexed duration-end scheduling (implemented)
25. OBS lifecycle auto-triggers
26. Media clock chase
27. Stop-mid-span teardown policy
28. Expanded easing and step modes
29. Finite loop count
30. Tap tempo

## 31–40: Macro Strip

31. Live action state feedback
32. Pages/banks (implemented: named tabs and native individual docks; persisted stable IDs, active tab, independent contents/orientation and frontend dock placement)
33. Drag reordering and separators (implemented: layout-edit grips, axis reorder, Shift spacing-only drag, Escape cancellation, persisted per-control gaps in place of spacer widgets per user request)
34. Opt-in frontend hotkeys
35. Undo/redo and duplication (partial: per-strip order/spacing undo/redo, 32 session-local edits; general control editing history and duplication remain open)
36. Custom ranges, inversion and double-click reset
37. Momentary action and toggle mode
38. Smooth timed preset/A-B glides
39. Multi-row flow/wrap
40. Quick search/filter

## 41–50: Actions and parameters

41. UUID filter visibility targeting (implemented)
42. Native scene-item transform domain
43. Enum/list control and cues
44. Color/alpha control and ramps
45. Studio preview and transition actions
46. Output and production actions
47. Extended native mixer
48. Multi-parameter snapshots
49. Generalized conditional predicates
50. Safe asynchronous HTTP/webhook action

## 51–60: Runtime efficiency

51. Digit pixmap cache (implemented)
52. Viewport-culled grid/ruler drawing (implemented)
53. Centralized MacroParameterMonitor (implemented: one GUI-thread poll, shared per-binding samples, hidden widgets skipped)
54. Event-driven OBS signal readback
55. Incremental dirty-item updates (partial: unchanged geometry, color and renderer updates skipped)
56. Display-rate UI tick throttling (implemented)
57. Delta undo/redo history
58. Signal-invalidated property schema cache; preserve validation under schema mutations
59. Legacy name lookup optimization/upgrade hint
60. Unified marker hit testing/geometry (partial)

## 61–70: Integration

61. Full inbound WebSocket vendor requests
62. Rich outbound telemetry (partial)
63. Bidirectional MIDI
64. Optional OSC
65. Declarative adapter registry
66. Live Downstream Keyer introspection
67. Audio peak/volume meter integration
68. Versioned C ABI
69. DAW/CSV/Audacity marker import
70. Generic in-process vendor call cue

## 71–80: UX and accessibility

71. Optional categorized track view
72. Zoom-adaptive inline duration labels (implemented: fit-aware, live resize updates, separate placement for ramp curves)
73. Native HiDPI SVG rasterization
74. Keyboard and screen-reader support
75. Interactive minimap
76. Exact-value popovers
77. Target coloring and swatches
78. Rich live-resolved tooltips
79. Localization coverage
80. Automation HUD and budget meter

## 81–90: Reliability

81. Explicit schema version and unknown-field preservation
82. Rotating config backups
83. Execution log/timing diagnostics
84. Import target-remapping wizard
85. Shared headless serialization (implemented)
86. Nested execution bounds (implemented)
87. Visible save/load error feedback
88. Dirty shutdown saves (partial)
89. Atomic integrations.ini persistence
90. Transition-override cleanup before scene-collection switch

## 91–100: Distribution and maintenance

91. macOS OBS 33 support
92. Automated real-OBS smoke CI
93. Windows/macOS QtTest CI
94. Scheduler and parameter benchmarks
95. ASan/UBSan CI
96. Guided Windows installer with major-version conflict detection
97. Cohesive TimelineDock submodules
98. Unified typed TargetRef codec
99. Interactive starter templates
100. CI localization and asset integrity gate
