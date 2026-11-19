<!-- meristem-template:v1 -->
# Active Work — Timeline preview readiness

### Freeze Time — 2026-11-19
- User requested a timed clock hold to compress long gaps visually. Appended event ID 15, separate real-time wait payload 0–24h (positive for valid cue), single labeled clock marker, native theme highlight for frozen clock and remaining tooltip. Same-time cues dispatch together; holds accumulate; later starts and ends shift; pause preserves remaining, seek/restart cancel, loop/end and headless clocks respect holds. OBS sources and independently running children continue. Active hold progress not persisted. Full wait starts at actual dispatch, including late wakeups.
- RED absent event type; focused scheduler/roundtrip/long-marker tests green. Full suite initially caught old picker-count assertion and test invoking inactive timer; updated test to start playback and retained original loop tick semantics. Native strict 93 passed, 0 failed/skipped. Logs /tmp/timeline-freeze-full2.log. Formatter check before commit. No install/push.

### Move undo/redo to menu — 2026-11-19
- User reported that the joined history buttons still did not visually match zoom and requested moving them to the menu. Removed both toolbar buttons and their container. Native menu actions at the top of the existing Timeline menu show localized labels and platform shortcut hints. Retained existing dock-scoped QShortcuts, history enable/disable checks, playback/drag guards and history model. Zoom styling and sizes unchanged.
- RED: updated existing dock tests failed for missing menu actions and unwanted toolbar history buttons. GREEN: tests click actual menu actions, verify shortcut behavior, history lockout and persistence, plus compact zoom geometry. Native OBS32 rebuild and full strict suite: 89 passed, 0 failed/skipped; exact clang-format 19.1.1 passed. Logs /tmp/timeline-history-menu-red.log, /tmp/timeline-history-menu-green.log and /tmp/timeline-history-menu-full.log.
- No installation or push. Native OBS visual acceptance remains outstanding.


### Theme awareness correction — 2026-11-18
- User asked whether all controls are theme-aware. Audited actual widgets and found valid dark foregrounds were being replaced by light fallback colors, ruler/grid color caches could stay stale, and Macro Strip rebuilt controls and scheduled a save on theme changes (losing text drafts).
- New Qt controls inherit native theme styling; fixed older custom painting to respect valid foreground roles, toolbar IconaMoon icons use ButtonText/HighlightedText, caption pills use the native palette, editor swatch borders and time hint no longer force black/gray. Timeline refresh updates ruler/grid caches, playhead and handles, end-marker overlay, drag guide and empty hint. Macro Strip recalculates themed row heights and repaints existing widgets rather than rebuilding. Semantic event colors and custom artwork unchanged.
- RED: light palette foreground assertion and preservation of a live-text draft both failed on old code. GREEN: both cases and themed control height pass. Native OBS32 build and strict suite: 89 passed, 0 failed/skipped; exact clang-format 19.1.1 check passed. Logs /tmp/timeline-theme-red.log, /tmp/timeline-theme-green.log and /tmp/timeline-theme-full.log. No install/push; full native OBS theme matrix and actual appearance not manually verified.


### Boolean and text parameter controls — 2026-11-18
- User requested boolean toggles and string entry. Existing boolean toggle discovery verified through the assignment dialog; added a distinct text binding/control type and typed cue value. Ordinary single-line and multiline editable properties are discovered, excluding password/info/hidden/disabled fields. Text updates use explicit Apply or modal OK; live readback preserves modified drafts. Multiline editing uses a popup to retain compact strip height. Both manual and scheduled string writes revalidate schema, support empty/Unicode/multiline values, and cannot redirect missing UUIDs.
- RED: absent text API reproduced by new regression compilation failure. GREEN: real libobs property discovery, actual assignment dialog for both text and bool, macro serialization, explicit Apply, readback, multiline modal and defined-value dialog, scheduled execution, excluded fields and removed target. Full strict native OBS32 suite 87 passed, zero failures/skips; clang-format 19.1.1 passed. Logs /tmp/timeline-text-red.log, /tmp/timeline-text-green3.log and /tmp/timeline-text-full.log.
- Native Enter signal connected, but initial Xvfb synthetic Return test did not commit; explicit Apply is verified by widget click. Native OBS keyboard routing, theme appearance, and real OBS text-source rendering remain unverified. No native install/push; installed runtime remains 68ab09a. Older builds cannot read new text-control macros safely; keep backups.


### Native audio mixer controls — 2026-11-18
- User invoked Remote Desktop Commander following the Control Panel adaptation discussion. Continued the agreed native audio-control direction: shared parameter editor discovers source-level volume and mute when libobs exposes audio capability. Separate optional mixer domain prevents conflating native audio controls with arbitrary plugin settings. UUID binding, slider/knob/toggle readback and instantaneous scheduled value cues reuse the existing implementation. Independent implementation using libobs APIs; no upstream code imported.
- Volume uses OBS_FADER_LOG conversion, normalized 0–100% fader travel in 0.1% increments, dB caption, silence to unity, no positive gain. Readback above unity saturates. Mute does not change volume. Native writes avoid property-schema enumeration. No meter, volume ramp, or action-button state feedback included.
- RED: new binding-domain regression could not compile on previous API. GREEN: real registered audio-capable source, actual libobs fader comparison, editor discovery, macro and cue payload roundtrip, slider readback and write, scheduled volume/mute, removed-target failure. Full native OBS32 build and strict suite 86 passed, zero failures/skips; exact clang-format 19.1.1 check passed. Logs /tmp/timeline-mixer-red.log and /tmp/timeline-mixer-full.log. No rendered/audio-output or native mixer GUI acceptance test.
- No install/push; active OBS left running at prior installed checkpoint 68ab09a.


### Owner-aware hotkey binding — 2026-11-18
- Following review of Voidscape-Development/Control-Panel, independently implemented stable hotkey ownership in our existing HotkeyCatalog. No upstream code imported or license change. New selections save a versioned identity payload with registerer type, owner and internal name, keeping legacy names for display. Source UUIDs survive owner rename and re-registration; missing/duplicate complete identities skip execution. Catalog excludes ambiguous exact identities; search cannot silently change an explicitly selected owner.
- Legacy actions retain name-only first match until explicitly retargeted. Older builds ignore the new payload; avoid executing new exact bindings on those builds. Output/encoder/service identity uses internal name (not UUID); frontend hotkeys have no exposed owner identity.
- RED: missing identity in editor with two real registered source hotkeys of the same name. GREEN: tests exercise actual routed press/release dispatch (test harness enables OBS callback routing), rename, re-registration, ambiguous identities, deletion, malformed explicit data, legacy execution and timeline/macro serialization. Full native OBS32 build and strict Qt/libobs suite 85 passed, no failures/skips. Exact clang-format 19.1.1 check passed. Logs /tmp/timeline-hotkey-red.log and /tmp/timeline-hotkey-full.log.
- Scope: this checkpoint addresses hotkeys only. Native faders, live state feedback and optional extension API remain candidates, not implemented. No installation/push. OBS running; installed runtime remains 68ab09a. Native frontend interaction and non-source hotkey owner cases not independently tested.


### Joined undo/redo buttons — 2026-11-18
- User requests undo/redo matching the existing zoom pair and retaining the existing Moon icon family. Both groups now use one shared joined-button builder with zero spacing, fixed content size and matching edge styles. Undo/redo retains its existing icons, tooltips, shortcuts and enabled-state behavior; each button matches zoom at 24px normally and 20px in compact mode.
- RED: compact layout test failed against the old separate buttons; initial shared layout still allowed extra space during compact resizing, corrected with a fixed-size layout constraint. GREEN: geometry, style and icon-size equality verified in normal/compact/load paths, existing undo/redo interactions pass. Native OBS32 rebuild and full strict Qt/libobs suite: 84 passed, 0 failed, 0 skipped. Exact clang-format 19.1.1 check passed. Logs /tmp/timeline-history-group-red.log and /tmp/timeline-history-group-full.log.
- No installation or push; visual acceptance in native OBS remains outstanding.


### CI formatting correction — 2026-11-18
- Push at `72e6c51` triggered formatting failures in GitHub Actions run 44422058880: full logs identify 17 C++ files and tests/CMakeLists.txt. Landlock messages are runner warnings, not the formatter failure.
- Reproduced all failures locally with the exact CI versions (clang-format 19.1.1, gersemi 0.21.0) in a temporary uv virtual environment. Applied the repository formatting configuration to the changed C++/CMake files; both repository check scripts now pass with `--fail-error --check`. No intended behavioral changes or CI bypass.
- Native OBS32 module rebuilt and full strict Qt/libobs suite passed again: 84 passed, 0 failed, 0 skipped. Before future pushes, run both exact-version format checks on all changed relevant files in addition to runtime tests. Remote CI for the corrective commit remains pending at this checkpoint.


### Documentation and synchronization checkpoint — 2026-11-18
- Runtime baseline: `59c3c25`, native OBS32 build, 84 strict Qt/libobs tests passed with no failures/skips. README, architecture, release checklist and durable project context reconciled with the implemented editor, scheduling, dynamic parameter controls and layout changes.
- User requested pushing the verified local series to existing main. The historical notes below retain their original per-checkpoint install/push status. Last installed runtime remains `3681512`; newer changes need native OBS interactive acceptance. No release, tag or install in this documentation pass.

### Dock height constraints and restored Timeline compact mode — 2026-11-18
- User requests non-cropping height limits and restored compact mode. Timeline compact toggle/load now updates toolbar/timecode, marker size and lane assignments; zoom also recomputes lanes. Viewport minimum includes a complete single event row, ruler, selection/hover padding, horizontal scrollbar and frame. Additional lanes extend scene height and can be reached via vertical scrollbar or Shift+wheel, without forcing an unbounded dock height. Grid/playhead/end/drag markers cover the full scene height; horizontal playhead following preserves vertical view. Geometry notifications corrected for label/duration changes.
- Macro Strip measures caption and actual themed slider/toggle dimensions, retains the intentionally sized knob, applies common row heights to action and live controls, and reserves scrollbar space. Inner content/scroll height constrained; outer dock remains resizable. RED: compact toolbar remained 32px instead of 28px; oversized 42px checkbox was clipped. GREEN: focused tests and native OBS32 build/full strict Xvfb Qt/libobs suite 84 passed, zero failures/skips (logs /tmp/timeline-height-red.log, /tmp/timeline-strip-height-red.log, /tmp/timeline-height-full.log).
- Deliberate choice: vertical overflow scrolls the scene (including ruler) instead of compressing or hiding extra lanes. Native OBS dock appearance and subjective layout still need user testing. No installation or push; normal OBS running.


### Relative Macro Strip knob interaction — 2026-11-18
- User reports the stock knob interaction feels bad. Reproduced absolute-angle jump (press changed step 25 to step 3 without dragging). Replaced stock mouse-angle behavior with relative vertical dragging, fractional movement accumulator, clamped endpoints with immediate reversal, Shift fine mode, focused wheel/arrow steps and no unfocused wheel capture. Native dial rendering and saved bindings unchanged.
- Avoid successful write readback interrupting an active slider/knob gesture; final release synchronizes, failed writes still validate the binding. Gesture test verifies no click jump, live write/readback, fine drag, endpoint reversal, release, keyboard handler and wheel focus behavior.
- RED: /tmp/timeline-knob-red.log. GREEN: native OBS32 build and full strict Xvfb Qt/libobs suite 82 passed, zero failures/skips (/tmp/timeline-knob-full.log). Direct Qt key event handler verified, real native OBS shortcut routing and subjective mouse feel not yet verified.
- Sensitivity chosen for first pass: 160 logical pixels/full range, up to eight pixels per step for small discrete ranges; Shift scales motion to 10%. No install/push; normal OBS remains running and must not be interrupted.


### Shared strip alignment and scheduled defined parameter values — 2026-11-18
- User requests aligned labels/vertically centered controls and scheduled parameter value assignment. Shared Macro Strip label row (18 px), control row (34 px), center and caption font across the action bubble and native controls. Geometry regression tests all three native control kinds.
- Added SetParameterValue (append-only ID 14) with versioned string payload, existing live property catalog and assignment dialog, numeric or On/Off value fields. No writes while configuring; cue sets one property at start, with live bounds/step validation and no duration-end action. Shared executor supports timeline and immediate Macro Strip action buttons. Missing/invalid bindings fail closed; ordinary saved event IDs unchanged. Existing live controls read back the value.
- RED: new tests failed compilation before API/layout existed. GREEN: native OBS32 build and strict Xvfb Qt/libobs suite 81 passed, zero failures/skips. Tests cover editor, saved round-trip, actual scheduled execution, rename, invalid payload, integer/boolean private-filter writes, Macro Strip readback, and aligned widget geometry; updated action-category count for the new type. Logs /tmp/timeline-defined-red.log, /tmp/timeline-defined-full2.log.
- Instant steps only (not ramps/keyframes); scalar properties only. GUI geometry verified in Qt harness, not on the user's live OBS dock; actual target plugin rendering not verified in this pass. Normal OBS is running; do not interrupt it or overwrite the loaded installation. Build is local, no push.


### Live Macro Strip target and property discovery — 2026-11-18
- User explicitly requested dynamic discovery. Existing libobs enumeration and live property introspection already worked at dialog opening, but the assignment dialog held a static catalog until reopened.
- Added one-second refresh of target catalog and selected target's schema while the assignment dialog is open. Compares records before rebuilding UI, preserves selection by source/filter UUID and property key/type, retains chosen control kind, defers dropdown refresh while popup is open, and clears removed choices without substitution. Final write and binding acceptance retain live schema validation.
- RED: new real Qt/libobs regression failed because a source added after dialog opening never appeared. GREEN: test verifies new source, rename, newly attached private filter in a grouped source, deduplicated filter identity, dynamic replacement of a property, explicit reassignment and source removal. Native OBS32 build and strict Xvfb suite 78 passed, 0 failed, 0 skipped. Logs /tmp/timeline-discovery-red.log and /tmp/timeline-discovery-full.log.
- Dynamic discovery remains limited to exposed visible/enabled numeric and boolean properties. Does not enumerate every target's property tree on each refresh. No profile modification, install or push; native OBS GUI and other OS/version verification outstanding.

### Macro Strip live source/filter parameter controls — 2026-11-18
- User wants assignable sliders, knobs and other controls targeting specific source/filter parameters. Implemented mixed legacy action buttons and slider/knob/toggle controls through the existing Macro Strip model, add menu, editor, context menu, reorder and autosave. Timeline scheduling unchanged.
- Targets include nested/grouped sources, standalone sources and their attached filters. Bind using source/filter UUID plus stable property key and scalar type. Read live settings, inherit min/max/step/suffix, recurse visible enabled property groups; refuse missing/hidden/changed-type parameters. No name fallback or initial writes. Poll visible controls, block feedback signals, coalesce continuous writes at 80 ms and flush on release; boolean immediate. Write only a typed settings patch and let OBS/plugin update callbacks apply it. New control records use type -1 to prevent older builds executing them; old builds may discard them on save.
- RED: new tests initially failed compilation (missing API); first widget test found deferred-deleted hidden controls, corrected retired widget visibility/test selection. GREEN: native OBS32 build and strict Xvfb Qt/libobs suite 77 passed, 0 failed, 0 skipped. Real fixture source and private filter tests verify grouped discovery, actual update callbacks, quantization, live readback, external changes, untouched settings, rename, detach, mixed serialization and assignment dialog including removed property handling. Logs /tmp/timeline-parameter-red.log, /tmp/timeline-parameter-green3.log, /tmp/timeline-parameter-full.log.
- Isolated smoke against actual locally built HyperShaderFX module passed: Effect Opacity 1.0 -> 0.37; shader-specific param_flow_speed -> 2.45 (script /tmp/timeline-hs-parameter-smoke.cpp, log /tmp/timeline-hs-parameter-smoke.log). Real plugin property/settings integration, not pixel/render verification. No normal OBS profile or HyperShaderFX code changes.
- First slice supports numeric and boolean settings only; list/color/text, custom ranges, direct volume/transform properties, animation/keyframing and throughput profiling not included. Full OBS frontend UI and other-platform qualification remain open. No installation/push.

### Undo/redo for Timeline event editing — 2026-11-18
- User requested undo/redo. Added compact icon buttons next to Add Event, native dock-scoped Undo/Redo shortcuts, disabled state during playback/drag/load, and session-only bounded 100-edit history. Complete value snapshots preserve event identity, precision, payload and visual properties. Undo/redo follow ordinary modelChanged/rebuild/autosave; do not execute OBS actions. Successful load clears both stacks; no-op edits keep redo, new edits branch history. Headless models do not retain history.
- Batch paste, delete and arrow-key nudge now emit a single model mutation and undo step. Actual dock tests revealed preexisting drag release consumption, unlimited end time mapped to a zero movement limit, view swallowing arrow keys, end-marker clamping of restored marker display, and exact-time dialog changes overwritten by the dock. Corrected these paths; unchanged fractional event times remain intact. No new serialized fields.
- RED: history API regression initially failed compilation; dock tests uncovered drag, arrow nudge and boundary restore failures. GREEN: native OBS32 build and strict Xvfb Qt/libobs suite 75 passed, 0 failed, 0 skipped. Real Qt dock tests cover buttons, native shortcuts, grouped paste/delete/nudge, single-marker drag, exact-time editor accepted path, playback locks, autosave and reload in a temporary config. Other tests cover all-field restoration, redo branches, bounded history, no-op, failed import, playback isolation, fractional time and off-boundary restoration.
- Scope: event edits only; settings, playhead, selection, Macro Strip history and full multi-marker drag not included. Snapshot memory scales with event count; capped at 100 edit steps. GUI tests use real Qt widgets and libobs with mocked frontend, not full native OBS UI or other platform validation. No install/push/normal profile change. Logs /tmp/timeline-undo-red.log, /tmp/timeline-undo-drag-red.log, /tmp/timeline-undo-extra-red.log, /tmp/timeline-undo-full.log.

### Chronological playback and end-marker accuracy — 2026-11-18
- Direction: keep the timeline and precise cue sequencing central; prioritize practical editing/playback improvements over a generic event-driven automation engine. Earlier state-trigger suggestions are not the current direction.
- Reproduced three scheduler faults: a late tick crossing the end marker skipped final starts/ends; overdue starts fired before earlier duration ends; loops reset before draining the outgoing cycle.
- Fixed chronological due-cue dispatch, duration ends before new starts at equal timestamps, inclusive final-cue processing, exact stopped playhead, and outgoing-loop completion before new zero-time cues. Uses steady_clock to avoid wall-clock discontinuities while retaining the existing start cursor and duration-end scan.
- RED: all three new scheduler regressions failed on prior code. GREEN: native OBS32 module build and strict Xvfb Qt/libobs suite 68 passed, 0 failed, 0 skipped. Tests exercise real scheduler signals and a simulated blocked event loop, not live OBS rendering or hardware timing. Logs /tmp/timeline-boundary-red.log and /tmp/timeline-boundary-full.log.
- Existing seek semantics, loop restart at zero, explicit end-marker truncation of longer durations, and persistence format retained. Late cues cannot recreate elapsed presentation time. No install, push, or profile change; real GUI and other platform/version checks remain open.

### Exact-time editing and drag preview — 2026-11-18
- Corrected a Timeline editing defect: grid snapping previously applied inside ItemPositionChange during zoom, compact-mode changes, stacking and exact-time model updates. A 2345 ms marker displayed at 2500 ms under a 500 ms grid. Snap now applies only to an active user drag, not layout updates.
- Drag guide now reports the post-clamp position, so it agrees with the visible marker and committed time at the timeline boundary.
- RED: both new QGraphicsScene regressions failed on the existing implementation (2345 became 2500; preview 2000 vs clamped 1750). GREEN: native OBS32 module built; strict Xvfb Qt/libobs suite 65 passed, 0 failed, 0 skipped. Tests cover zoom, compact mode, stacking, exact time updates, snapping while dragging and end-marker clamping.
- Scope is the existing Timeline editor; no event schema change, new trigger model, installation or profile modification. Manual GUI and cross-platform checks remain outstanding. Logs /tmp/timeline-snap-red.log and /tmp/timeline-snap-full.log.

### Media playback actions — 2026-11-18
- Shared Timeline/Macro Strip Media Playback now offers Restart, Stop, Pause, Resume and Seek to (absolute seconds, millisecond precision). Timed Pause resumes at the end and timed Resume pauses at the end; Seek is instant and clamps to known duration. Legacy Restart/Stop inverses retained. New numeric IDs 11–13 appended; historical ID 3 stays reserved.
- Media picker uses OBS controllable-media output flag, current scene then other scene collection roots, nested groups/scenes, source UUID deduplication. New media target data string carries UUID and seek milliseconds; legacy name-based actions remain readable. Missing explicit UUID and malformed versioned data refuse execution; no silent retarget. Older binaries do not understand new actions/target identity.
- RED: editor regression failed with only two media actions. GREEN: strict OBS32 Xvfb suite 63 passed, 0 failed, 0 skipped; full plugin build succeeds. Real registered libobs controllable-media fixture validates asynchronous restart/pause/resume/seek/stop, timed inverses, clamped seek, group-only discovery, duplicate deduplication, rename and deletion, no fallback, and reserved ID no-op. Editor and Timeline/Macro JSON round trips verified. Logs /tmp/timeline-media-red.log, /tmp/timeline-media-runtime2.log, /tmp/timeline-media-full.log.
- Test fixture is not FFmpeg/VLC playback; actual decoder seeking, GUI interaction and other OBS/platform versions remain unqualified. No push, install, or normal profile changes. Next candidate is OBS/media-driven trigger conditions, not implemented here.

### Nested scene item targeting — 2026-11-18
- Independently implemented the useful scene-traversal mechanism found in bitwraith21/obs-controls-toolkit (no upstream license identified; no source copied). Shared Timeline/Macro Strip Source Visibility picker now lists exact items in current scene, nested scenes and groups with path and ID; shared containers visited once.
- Existing name-based actions retain their behavior; new actions encode owner UUID, scene item ID and source UUID in the existing string data field. Missing/invalid explicit targets fail closed instead of redirecting. Scene/source renames and program changes do not retarget. Older plugin binaries do not understand exact targeting and should not execute these new actions. Parent visibility is unchanged.
- RED: real libobs nested scene item absent from editor. GREEN: module built against OBS32, strict Xvfb Qt/libobs suite 61 passed, 0 failed, 0 skipped; regression covers nested scenes, groups, duplicate references to a source, renames, Timeline/Macro JSON, missing target editor preservation and no fallback on deletion.
- Native OBS GUI interaction and OBS31/33/cross-platform builds not yet verified for this slice; no install, push, or ordinary OBS profile changes. Further candidate harvesting: extended media controls/state-driven triggers, not yet implemented.

### OBS33 CI runner mismatch — 2026-11-12
- GitHub Actions run 43324596988, job 131553756673, advertised \`(obs33, ubuntu-26.04)\` but booted Ubuntu 24.04.5 because \`.github/workflows/linux-build.yml\` pinned \`runs-on: ubuntu-24.04\` and ignored the matrix OS. OBS33 configure failed requiring FFmpeg >=8; Ubuntu24.04 supplies 6.1.
- Changed only Linux CI job's runner selector to the existing matrix OS, preserving 24.04 for OBS31/32 and activating 26.04 for OBS33. Strengthened \`tests/test_obs33_readiness.py\` to reject a hard-coded runner. RED assertion failed before fix, GREEN after; strict local OBS32 suite 60 passed, 0 failed, 0 skipped.
- This tests the scheduling contract and existing local OBS32 behavior, **not** a completed new GitHub-hosted OBS33 build. Inspect new CI and address any separate failures against actual logs.

### Native OBS32 local installation checkpoint — 2026-11-12
- User-requested installation completed for native OBS Studio 32.2.0 (not the separate OBS33 Flatpak). Installed local commit `6224f64` using the project's CMake install rules; module and en-US locale updated. Source tree had been clean.
- Strict OBS32 Qt/libobs Xvfb suite **60 passed, 0 failed, 0 skipped** before installation. Installed module Build ID matches the verified build (CMake strips RUNPATH on install); `ldd -r` resolves dependencies and symbols. Installed locale matches source byte-for-byte.
- Safeguarded previous plugin and Timeline configuration in a private, local rollback archive (path held outside public repo). Checksums of timeline.json, macro_buttons.json (config and legacy installed data) unchanged after installation.
- Isolated OBS32 GUI-process startup with temporary XDG config and real installed plugin logged successful registration of Timeline dock, Macro Strip dock and WebSocket vendor. This is a real plugin-load check, not a manual visual interaction smoke. Normal OBS was closed and its scenes/profile were not opened or modified. OBS33 build of these newest changes remains unverified.
- No push, OBS33 installation or profile migration performed.

### Grouped and searchable hotkey selector — 2026-11-12
- Grouped OBS hotkey picker by real libobs hotkey registerer type: source-bound, frontend/plugins (plugin identity not introspectable), outputs, encoders and services. Source-bound hotkeys match known Move filter IDs and the filter's registered hotkey name to show **Move · source** when grounded; other source hotkeys appear under their source. No blanket plugin-name inference.
- Added case-insensitive search across description, stable OBS hotkey ID and group/source. Group headings are non-selectable; an empty search result disables selection. Saved missing hotkey IDs remain editable and are not silently swapped for another hotkey.
- RED: new test compilation failed before the catalog existed; real hotkey owner test then caught an incorrect source-reference interpretation. GREEN: OBS32 module builds and strict Xvfb/libobs suite **60 passed, 0 failed, 0 skipped**. Tests exercise real libobs frontend/source registration and weak-source reference handling, Move identity classification, UI grouping, search and saved-ID preservation. The wrong direct-source-pointer interpretation was detected by failing regression and corrected to a strong reference from the registered weak source, with balanced release.
- Scope is the shared Timeline/Macro Strip editor only; the native OBS Settings hotkey screen and installation stay untouched. Actual live Move plugin labeling inside OBS GUI remains unverified.
### Unified source/filter/media action picker — 2026-11-12
- Combined paired editor choices into Source Visibility (Show/Hide), Media Playback (Restart/Stop), and Filter Visibility (Enable/Disable), for both Timeline and shared Macro Strip editor.
- UI grouping only: numeric event type IDs, JSON formats, executor behavior and timed inverses remain unchanged. Old saved variants preselect the appropriate operation. Missing historical targets remain selectable during editing rather than being overwritten by placeholder values.
- TDD: both new UI tests failed before adding the action selector, then passed. Native OBS32 module and strict Xvfb/libobs suite **57 passed, 0 failed, 0 skipped**. Coverage includes grouped options, historical variants and durations, timeline and Macro Strip JSON types, and missing target preservation. Full production GUI smoke and OBS33 build of this new slice not performed.

### Loaded-plugin selection menu — 2026-11-12
- Requested capability: Timeline settings → Plugins & Integrations shows **loaded** OBS modules, supports selecting implemented action adapters, and updates available new actions in both Timeline and Macro Strip (shared editor). Unknown loaded plugins appear without false promises of action support.
- Integration visibility is persisted independently in `integrations.ini` inside the OBS module configuration directory. This does not enable/disable OBS plugins and deliberately does not disable or remove previously saved actions. Saved Downstream Keyer events retain their editable fields even if the adapter is hidden or the plugin is absent.
- First adapter: Downstream Keyer; actions only offered when its loaded module is detected and the user has enabled the adapter. Unavailable action attempts are still handled by existing executor logging.
- RED: new tests failed before the registry existed. GREEN: OBS32 plugin module and Qt harness built; strict Xvfb/libobs suite **54 passed, 0 failed, 0 skipped**. Regressions cover loaded/supported/unsupported module listing, menu checkbox toggling, persistence, available new-editor choices, and unavailable saved-event editing. Real OBS menu rendering with third-party DSK, vendor readiness, and cross-platform use remain unverified. No OBS installation or profile modified.

### Optional downstream-keyer scene control — 2026-11-12

- Added a new appended `DownstreamKeyerScene` event type, shared by Timeline and Macro Strip. Name a DSK, overlay scene and optional view/canvas; blank scene clears the DSK overlay, and a timed scene selection clears on event end.
- Uses OBS WebSocket's **in-process** `CallVendorRequest` for `downstream-keyer / dsk_select_scene` without a TCP client, credentials or any modifications to the DSK plugin.
- Added editor fields and English strings, persisted serialization support, icon and color, build-time API fallback, and explicit logs for missing/rejected requests.
- RED: new C++ regression failed to build because the event type was absent. GREEN: OBS32 native module/test executable built; strict Xvfb/libobs Qt test suite passed **51/51 tests, zero skips** after fixing the fixture's retained OBS scene-source registry state. New tests check vendor payload, timed hide, empty-name guard, editor values and both macro/timeline JSON round trips. Upstream DSK source confirms a blank scene clears via `SwitchToScene`.
- Scope: no real OBS installation, OBS profiles/scenes, or other plugin repositories changed. **Actual two-plugin runtime dispatch and visual composition remain unverified**; an isolated OBS session with Downstream Keyer installed is the next acceptance gate. OBS 33 and cross-platform DSK behavior have not been validated for this slice. The pre-existing main branch was four commits behind `origin/main`; no fetch/merge/push was performed.


## Outcome

Bring Timeline toward a verified first public preview with clean runtime packages, accurate documentation, and durable Meristem project context.

## Why it matters

Timeline is the maintainer's first public OBS plugin. Its runtime functionality and migration safety matter more than extra features or green compilation alone.

## Current state

### OBS 33 Linux compatibility checkpoint — 2026-10-30
- **Source/config:** Pinned Linux OBS 33.0.1 upstream tarball (SHA-256 verified), added explicit `obs33` compatibility selection, version guard, packaging label/dependency, and an Ubuntu 26.04 Linux CI matrix entry. Kept cross-platform artifacts and existing OBS31/OBS32 behavior unchanged.
- **Red/green:** OBS33 was previously rejected at CMake bootstrap. After changes an isolated Linux module compiled using real OBS33 Flatpak 33.0.0 SDK headers/libraries and passed relocation check in the Flatpak runtime. A mixed host/Flatpak Qt test executable *did not link* due Qt6.4/6.11, FFmpeg and glibc ABI differences; do not claim OBS33 runtime tests pass from this local probe.
- **OBS32 protection:** CMake native OBS32 rebuild and strict Xvfb Qt CTest passed on the same checkout; real OBS profile and installed module untouched.
- **CI repair and verification:** Cherry-picked previously green Ubuntu 24.04 runner fix from `90fdc27` as `8f36ea6`. OBS33 compatibility commit `0b67552` was pushed to `main`. Exact-head [Linux CI run 41154431438](https://github.com/myrqyry/timeline/actions/runs/41154431438) succeeded on all three targets (OBS31/32 Ubuntu24.04, OBS33 Ubuntu26.04); strict OBS33 Qt test logged **46 passed, 0 failed, 0 skipped**, with 3 residual allocations permitted by test cleanup. This verifies the pinned OBS33.0.1 Linux SDK build and headless suite, not interactive dock usage.
- **Still needed:** Isolated OBS33 live GUI module load/action/dock persistence smoke; full cross-platform OBS33 dependency pins/builds remain out of scope. Preserve user OBS32 install and settings.

- **Historical October 25 branch context:** `main` at `c490a40` included the documentation/substrate reconciliation after merged PR #3. Packaging hygiene, release documentation, Meristem adoption and the source-archive privacy fix (`de4f388`) are committed history.
- **Historical October 25 working tree:** Clean after pushing `c490a40`; the follow-up documented verified CI and maintainer's successful plugin-load report. See the October 30 checkpoint above for current code work.
- **Historical verification:** The six-platform/version build matrix and both formatting jobs passed for `c490a40`. Local strict Qt tests passed and the maintainer confirmed the installed plugin loaded. The separate Linux OBS31/OBS32 test workflow later failed on Ubuntu 26.04 teardown allocation accounting; a pinned-24.04 repair was independently CI-verified on `90fdc27` and incorporated above.

## Locked decisions

- Remove the obsolete pre-Meristem Hal Labs protocol boilerplate from root `AGENTS.md` rather than retaining it nostalgically.
- Preserve useful OBS-specific engineering rules and child instruction files.
- Meristem canonical project files are safe public project knowledge; the ignored `.meristem/local/` is private machine state.
- Preserve merged history, the normal OBS profile and existing config. The former draft follow-up PR #3 is now merged.

## Constraints / do-not-regress

- No undocumented change to runtime scheduling, transition lifetime, macro save/recovery, config paths or OBS SDK targeting.
- Do not expose credentials, raw conversations, local absolute workstation paths or personal continuity in canonical Git-tracked files.
- No unrequested merge, tag, public release or real OBS profile installation.

## Current objective

With Linux OBS31/32/33 exact-head CI green, complete isolated OBS33 live module/dock/action testing and remaining preview gates in `docs/RELEASE_CHECKLIST.md` before any release. Do not claim Windows/macOS OBS33 compatibility or a working user install from Linux CI alone.

## Verification

- Historical evidence recorded before this repair: PR #3 head `54040a4` passed [Linux tests](https://github.com/myrqyry/timeline/actions/runs/38859345025) and [six-platform matrix](https://github.com/myrqyry/timeline/actions/runs/38859345030).
- Local pre-Meristem cleanup: strict Xvfb 44 Qt tests, zero skipped; Debian package contained binary and locale but no `AGENTS.md`.
- Meristem initializer ran in **adopt** mode; doctor reported `ready` after semantic population. All six platform builds, strict OBS tests, and both format checks passed for `a265009`.
- Privacy regression RED: real CPack tarball included `.meristem/local/cpack-private-fixture.txt` despite `.gitignore`. GREEN: rebuilt tarball excludes the private directory and retains public `.meristem/PROJECT.md`. The synthetic fixture was removed after both runs.
- Documentation reconciliation (2026-10-25): local Ninja RelWithDebInfo configure/build with `TIMELINE_OBS_COMPATIBILITY=obs32` passed against installed libobs 32.0.1 and Qt 6.4.2. Strict Xvfb/software-GL CTest passed with 44 Qt tests, zero failures/skips. This uses the installed SDK, not the pinned CI OBS 32.2.1 build input. Meristem doctor reported `ready`; no package install or interactive OBS check was performed.
- Requested local installation (2026-10-25): the verified OBS32 binary and locale were installed atomically for OBS 32.2.0. Installed hashes match the staged CMake install, with no missing shared libraries. A rollback copy is retained under ignored `.meristem/local/`; existing saved data and legacy files were preserved. The running OBS process was not restarted, so loading the new binary remains unverified until restart.
- Post-install confirmation (2026-10-25): the maintainer reports that the installed plugin loaded successfully. This closes the pending local load check; it does not establish full editor/action coverage.
- Exact-head CI (2026-10-25): [build matrix and formatting](https://github.com/myrqyry/timeline/actions/runs/40243732211) passed for `c490a40`: Linux, Windows and macOS for both OBS31/OBS32, including the Ubuntu source-archive privacy checks. Release creation was skipped. The separate [Linux build/test workflow](https://github.com/myrqyry/timeline/actions/runs/40243732151) still reported both jobs in progress when checked.

## Open uncertainty

- Completion of the separate Linux build/test workflow for `c490a40` remains to be checked; the six-platform build/packaging matrix is green.
- Real Windows/macOS runtime loading, clean installation/upgrade and full interactive OBS action/editor coverage are not established. These remain in `docs/RELEASE_CHECKLIST.md`.
