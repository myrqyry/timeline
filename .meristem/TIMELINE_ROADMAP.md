# Timeline improvement program — 2026-11-27

Source: user-supplied "Architectural & Product Strategy Analysis: Timeline for OBS", 100 distinct enhancements. User requested implementation of all code fixes and improvements. This is the accepted work queue, not evidence that all features are absent or delivered. Reconcile each item with current source before changing it. Existing release gates remain in force; do not claim cross-platform or interactive OBS verification from Qt/libobs fixture tests.

## Checkpoints and status

- e28ebe3: #24, #51, #52, #85; #88 partial (protect unreadable startup file; dirty shutdown optimization remains).
- 5ae401b: #15, #41, #86; #62 partial (event ID, label, duration and nested flag).
- UI follow-up: #56, #10 partial (cursor anchor, not fit), #60 partial (ruler hit radius shared; narrow body hit zone and handle geometry remain). See git history and test results for verification.

All unmarked items remain open or require reconciliation. Implementation order: hot-path safety and efficiency, direct manipulation, shared typed target/control model, interoperability and creative extensions, then distribution and coverage. Do not introduce new work on the audio/video callback thread; preserve fail-closed targeting, undo semantics, load/save safety, and isolated test configurations.

## 1–10: Core editing

1. Direct duration-handle resizing (snapping, one undo)
2. Rubber-band selection and group dragging (one transaction)
3. Viewport-pinned time ruler
4. Visual ramp curve and direction
5. Per-cue mute/bypass
6. Single-cue test/run now
7. Broken-target health badges
8. Ripple insert/delete
9. Named ruler markers/bookmarks
10. Cursor-anchored zoom and zoom-to-fit (partial)

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
32. Pages/banks
33. Drag reordering and separators
34. Opt-in frontend hotkeys
35. Undo/redo and duplication
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
53. Centralized MacroParameterMonitor
54. Event-driven OBS signal readback
55. Incremental dirty-item updates
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
72. Zoom-adaptive inline duration labels
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
