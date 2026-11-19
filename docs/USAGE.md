# Timeline and Macro Strip usage guide

The [README](../README.md) is the quick start. This guide describes action
semantics, binding behavior and operational details for the current preview.

## Action behavior

### Scene switch

Optionally select a named transition. Timeline restores the previous
transition after the OBS frontend's transition duration, with a fallback
delay when OBS reports no duration. Rapid switches replace a pending
restoration timer rather than stacking restore callbacks.

### Source visibility

The editor lists scene items in the current scene, including groups and
nested scenes, with their paths and item IDs. New explicit targets use the
owner UUID, item ID and source UUID, so renames or program-scene switches
don't redirect the action. Missing explicit targets are skipped. A shared
nested scene has shared underlying scene items, and changing an item does
not change the visibility of its parent containers.

Legacy name-based actions keep their old lookup behavior until explicitly
retargeted. Older builds do not understand the new exact-target payload.

### Media playback

Restart, Stop, Pause, Resume and Seek to are grouped under Media Playback.
Restart invokes OBS's restart operation, rather than resuming a paused
source. The picker discovers controllable media sources across scenes,
including nested scenes and groups, and lists each source UUID once.
New bindings survive source renames; old name-based bindings retain their
original behavior.

Seek is in seconds with millisecond precision, clamped to known media
duration. Actual media playback depends on the source implementation.
Historical `MediaSeek` (ID 3) stays reserved; the new `MediaSeekTo` action
has a separate ID. Older builds may not understand current media targets.

### Durations

| Action | End action |
| --- | --- |
| Show / Hide | Hide / Show |
| Filter Enable / Disable | Disable / Enable |
| Media Restart / Stop | Stop / Restart |
| Media Pause / Resume | Resume / Pause |
| Downstream Keyer Scene | Hide overlay |

These are the historical default end actions. Enable **Duration**, then
choose **At end** to override that default. Visibility can explicitly
show/hide, filters enable/disable, media restart/stop/pause/resume, and
Downstream Keyer can hide or select another named overlay on the same keyer.
**Leave unchanged** performs no OBS operation at the end. These choices
operate on the original target; they are not snapshots of arbitrary prior
OBS state. Existing files without an ending state retain their default.
An unknown explicit ending state skips execution rather than reversing.
Older builds ignore explicit ending states, so use the current build for
these cues. Freeze Time delays duration endings along with other cues.
Scene switches, hotkeys, nested timeline triggers, seek and instantaneous
parameter cues do not have an automatic end action. Numeric parameter
ramps instead finish at their defined endpoint.

### Setting a parameter

Select **Set Parameter Value**, then **Choose parameter and value**. Use the
live source/filter picker to choose an exposed numeric, boolean or text property,
and define its value using the appropriate editor.

Configuration does not apply a value. Without Duration, the event sets the
property once, clamping and quantizing to the live range and step. A missing
or invalid binding is skipped instead of selecting a different target.
A value remains set until another action or manual adjustment changes it.

A Macro Strip action button can use the same event type as an immediate
preset. Live controls bound to that property reflect its changed value.

### Animating a parameter

In **Set Parameter Value**, select an integer or float property and set its
starting value. Enable **Duration**, enter the ending value, and select
an easing curve (Linear, Ease in, Ease out, or Ease in/out). The non-linear
curves use quadratic easing. The cue's duration bar represents the logical
animation span. The final value remains set after the span finishes.

Samples follow the timeline clock: pause and Freeze Time halt interpolation,
and playback resumes from the same logical progress. Scrubbing does not
apply OBS changes; starting playback within a ramp resumes at the
corresponding progress. Seeking past a completed ramp does not replay it.
A later numeric, boolean or text cue for the same exact binding in the
same timeline supersedes an earlier ramp, including its eventual ending.
Simultaneous ramps on different bindings can run together. A separate
timeline or manual control can still write the same property; there is
no global automation lock.

All samples respect the live property range and step. Coarse integer or
float steps produce stepped motion, and mixer volume interpolates in OBS
fader position rather than linear gain. Missing or incompatible targets
are skipped. Boolean and text values remain instantaneous. Macro Strip
does not offer scheduled ramps. Older builds reject the version-2
parameter payload instead of treating it as a version-1 instant value.
Do not resave new timelines using those builds.

### Freeze Time

Add a Freeze Time cue and set its wait in seconds (up to 24 hours). The
playhead holds at the marker while OBS continues running normally. The
clock resumes automatically; later cues and timeline-duration endings
are delayed without changing their saved timestamps. Cues on the same
marker timestamp still execute together, and coincident holds accumulate.
The clock border highlights while waiting; hover for remaining time.
Manual pause preserves remaining wait; seek cancels the active wait and
restart clears it. A new pass through the marker waits again. Nested
timelines use their own clock. Other OBS media and independent timers
are not paused. Active wait progress is not persisted across sessions.

### Finding hotkeys

Select **Hotkey Trigger** in either editor. Search by description, registered
ID or source/group. The picker groups recognized Move filter hotkeys by
their owning source, other source-registered hotkeys by source name, and
frontend/plugins, outputs, encoders and services separately.

Group headings aren't selectable. New hotkey bindings store registerer type,
owner identity (source UUID, or internal output/encoder/service name) and
the registered hotkey name. The current session's numeric hotkey ID is
resolved at execution. Source renames and hotkey re-registration do not
redirect the cue. Exact identities that are missing or ambiguous do not
execute. The picker excludes ambiguous complete identities; filtering a
saved choice out of the list does not silently select a replacement.

Legacy name-only hotkeys retain their former first-match behavior until
explicitly retargeted. Missing selections remain editable without losing
their stored binding. Older plugin builds ignore the new owner payload:
use the current build to execute these cues safely.
This picker does not change OBS Settings → Hotkeys or assign shortcuts.
OBS exposes registerer types but not reliable plugin identity for all
frontend hotkeys. Move animations are still performed by Move.

### Downstream Keyer scene

Requires Exeldro's Downstream Keyer and OBS WebSocket API support. The editor
accepts the keyer name and overlay scene; a blank scene hides the overlay.
A blank view/canvas addresses the primary output. The overlay scene must
already belong to the named keyer's scene list.

The request uses the in-process OBS WebSocket vendor API, not a remote
network connection, so no credentials or enabled remote server are needed.
Missing vendors, invalid names and unavailable APIs are logged.

## Timeline editing and playback

- Events can be placed at the playhead, a double-click or a right-click
  position. Exact time edits and layout changes preserve event timestamps.
- Choose Time mode for a manual snap interval or BPM mode for beat
  subdivisions. Zoom spans 5–1,000 pixels per second.
- Drag the playhead to seek and the optional end marker to set the limit.
- Overlapping events stack into lanes. Compact mode updates the toolbar,
  timecode and event dimensions and recalculates stacking. Extra lanes
  scroll vertically (including the ruler); ordinary wheel scrolling remains
  horizontal and Ctrl + wheel zooms.
- Cues exactly at a positive end marker execute before stopping or looping.
  Delayed ticks dispatch due starts/ends in chronological order, with ends
  before starts at equal timestamps. This cannot restore elapsed presentation
  time. Durations extending beyond an explicit end are not automatically
  shortened or cleaned up.
- OBS frontend hotkeys are registered for play/pause, stop and restart.

### Undo and redo

The Undo/Redo actions at the top of the Timeline menu and dock-local
shortcuts share the last 100 event edits
in memory. Add/remove, dragging, timestamps, labels, colors, icons, durations
and action parameters are restored. Batch paste, deletion and nudging take
one undo step. No-op edits retain redo; a new edit after undo discards redo.
A successful document load clears history.

History edits do not execute OBS actions. History is disabled while playing,
loading or dragging, and excludes playhead movement, zoom, grid settings,
end-marker changes, Macro Strip edits and selection. It is not saved to disk.
The normal autosave path persists the resulting event data.

## Live parameter controls

Macro Strip mixes immediate action buttons with live numeric sliders/knobs
boolean toggles and editable text controls. The shared parameter catalog walks scenes, nested
groups, standalone sources, attached filters and exposed property groups.
It reads the property's current type, range, increment and unit suffix.

Bindings store source UUID, optional filter UUID, property key and scalar
type. Renames retain identity. Added/removed targets and changed property
definitions are reflected in the open assignment dialog, refreshed about
once per second. Refresh is deferred while a dropdown is open or the
defined-value editor is being edited. Missing choices require reassignment.

Visible live controls read current settings about four times per second
and refresh their schemas about every two seconds. Loading macros doesn't
reset the source values. Changes are applied through a typed, one-property
OBS settings update; continuous adjustments are coalesced and flushed on
release, while toggles apply immediately. The target plugin remains
responsible for applying the setting.

Knobs use a 32-pixel rotary face with a theme-colored 270-degree progress
arc and position pointer. Relative vertical movement is unchanged:
160 logical pixels for a full wide-range drag, up to eight pixels per
discrete step for smaller ranges. Shift scales movement to one tenth.
There is no click-to-angle jump, and reversing at either limit responds
immediately. Wheel and arrow keys adjust one step when focused.
Parameter caption columns reserve width when created or when theme metrics
change, not when values or availability change. Long captions elide in
place, preserving control alignment and neighboring positions.

Only visible, enabled integer, float, boolean and ordinary editable text
settings are discoverable (numeric controls are limited to one million
increments). List/color/button properties, custom ranges, scene-item
transforms and manual ramp controls are not included in Macro Strip.

### Boolean and text controls

Boolean properties automatically offer a Toggle; the checked state tracks
the current OBS value. Text properties offer Text entry. Single-line fields
use an inline entry and an explicit **Apply** button, keeping unsubmitted
edits separate from the live value. External updates refresh clean entries
without replacing a draft. Multiline properties open an editable text
dialog, with OK to apply and Cancel to leave the source unchanged. This
keeps the strip compact regardless of text length. Drafts are not persisted.
The field retains normal text editing and clipboard behavior; use the
caption or surrounding control for the binding menu.

Text can also be assigned by Set Parameter Value, including empty strings,
Unicode and multiline content. The shared binding editor does not apply
text when configuring a cue. Password and informational text properties
are never offered. Writes revalidate the current property schema.

### Native mixer controls

Audio-capable sources expose **Mixer / Volume (fader position)** and
**Mixer / Mute** in the same parameter picker, even without corresponding
OBS settings properties. Use a slider or knob for volume, or a toggle for
mute. The volume uses the OBS logarithmic fader curve; 0–100% represents
fader travel in 0.1% increments, not a linear audio multiplier. Its live
caption shows the corresponding dB value. At zero it reads -inf dB and
at 100% it reaches unity (0 dB). Positive gain above unity is outside
this control's range; readback saturates at the maximum. Mute is independent
of volume. External mixer adjustments are read back without resetting values.
Both are available for instantaneous Set Parameter Value cues; volume
also supports numeric animation.

Missing sources, filters or properties disable the control rather than
redirecting it by name. The dock aligns its rows using the native theme's
control sizes and reserves room for the horizontal scrollbar. Both docks
follow OBS palette and theme changes, keeping event colors and custom SVG
artwork intact. Theme refreshes do not discard unfinished text edits.

## Plugins and integrations

Open **Settings → Plugins & Integrations** in Timeline to see loaded OBS
modules. A supported adapter has a checkbox that enables adding its action
in both editors. Downstream Keyer is the first such adapter; it requires
WebSocket support at build time.

The checkboxes control available new actions, not OBS plugin loading or
execution of previously saved events. Saved unavailable actions retain
their fields. Preferences are saved in `integrations.ini`.

Dedicated action adapters are separate from dynamic property bindings:
compatible scalar properties can be exposed by other plugins without a
Timeline-specific adapter.

When built with the optional OBS WebSocket vendor header, Timeline can
emit `TimelineEvent` and playback status events. Event notifications carry
the phase, numeric type, source name and time. OBS WebSocket owns
authentication and transport; Timeline has no separate network listener.

## Saving and recovery

Settings save in the writable per-plugin configuration directory using
atomic file writes. Timeline edits debounce for 250 ms, Macro Strip edits
for 300 ms. Timeline documents accept the current object format and legacy
bare event arrays. See the [file formats](ARCHITECTURE.md#persistence-and-file-formats).

Default Linux locations:

```text
~/.config/obs-studio/plugin_config/timeline/timeline.json
~/.config/obs-studio/plugin_config/timeline/macro_buttons.json
```

A valid legacy installed Macro Strip file at
`~/.config/obs-studio/plugins/timeline/data/macro_buttons.json` is imported
if the canonical config is absent, without deleting the original.
Scene-collection copies are recovery backups, not replacements for a
newer authoritative file or unsaved edits.

If saving Macro Strip fails, changes remain authoritative in memory until
a successful retry or shutdown. Check OBS logs and available storage
before exiting. Windows/macOS use their respective OBS configuration
locations.

Back up documents before downgrading. Old builds may ignore explicit
target identities or discard unsupported action IDs and parameter-control
records after saving. Never rely on an old build to execute a new UUID-based
action safely.

Imported files are trusted local configuration: playback can switch scenes,
change visibility, control media and properties, trigger OBS hotkeys and
run nested files.

## Further reading

[Build and installation](../PACKAGING.md) ·
[Architecture](ARCHITECTURE.md) ·
[Release checklist](RELEASE_CHECKLIST.md)
