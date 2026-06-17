---
name: obs-frontend-dev
description: "Build OBS Studio frontend plugins with Qt dock widgets, WebSocket vendor API, and settings persistence. Use for: dock plugins with save/load callbacks, WebSocket vendor registration and events, frontend event handling, hotkey management, obs_data_t persistence, scene/source enumeration. Triggers on OBS frontend plugins, obs_frontend_*, obs-websocket vendors, dock plugins, OBS module lifecycle."
---

# OBS Frontend Plugin Development

## Quick Start

### Module Entry Point

Every frontend plugin needs `plugin-main.cpp`:

```cpp
#include <obs-module.h>
#include <obs-frontend-api.h>
#include "my-dock.h"

static MyDock *g_dock = nullptr;

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("plugin-name", "en-US")

bool obs_module_load(void) {
    g_dock = new MyDock();
    obs_frontend_add_dock_by_id("my_dock", "My Plugin", g_dock);
    return true;
}

void obs_module_unload(void) {
    if (g_dock) {
        g_dock->deleteLater();
        g_dock = nullptr;
    }
}
```

### Build Commands

```bash
cmake -S . -B build \
  -DCMAKE_INSTALL_PREFIX="$HOME/.config/obs-studio/plugins/obs-frontend-plugin" \
  -DOBS_PLUGIN_DESTINATION="bin/64bit" \
  -DOBS_DATA_DESTINATION="data"

cmake --build build
cmake --install build
```

## Core Patterns

### Dock Widget Lifecycle

```cpp
// Constructor: register callbacks and hotkeys
MyDock::MyDock(QWidget *parent) : QWidget(parent) {
    obs_frontend_add_save_callback(saveCallback, this);
    obs_frontend_add_event_callback(frontendEvent, this);
    obs_hotkey_register_frontend("My.Action", "My: Action", hotkeyCallback, this);
}

// Destructor: unregister everything
MyDock::~MyDock() {
    obs_frontend_remove_save_callback(saveCallback, this);
    obs_frontend_remove_event_callback(frontendEvent, this);
    obs_hotkey_unregister(m_hotkeyId);
}
```

### Save/Load Pattern

```cpp
static void saveCallback(obs_data_t *save_data, bool saving, void *data) {
    MyDock *dock = static_cast<MyDock*>(data);
    if (saving) {
        // Embed your data into save_data
        obs_data_set_string(save_data, "my_setting", "value");
    } else {
        // Loading: read from save_data
        const char *val = obs_data_get_string(save_data, "my_setting");
    }
}

// Or save/load your own config file:
void MyDock::save() {
    char *path = obs_module_get_config_path(obs_current_module(), "config.json");
    if (!path) return;
    // write the serialized config to path
    bfree(path);  // REQUIRED
}

void MyDock::load() {
    char *path = obs_module_get_config_path(obs_current_module(), "config.json");
    if (!path) return;
    // read the serialized config from path
    bfree(path);  // REQUIRED
}
```

### Frontend Events

```cpp
static void frontendEvent(enum obs_frontend_event event, void *data) {
    MyDock *dock = static_cast<MyDock*>(data);
    switch (event) {
        case OBS_FRONTEND_EVENT_RECORDING_STARTED:
            // Update UI via invokeMethod for thread safety
            QMetaObject::invokeMethod(dock->recordButton, "setText",
                Qt::QueuedConnection, Q_ARG(QString, "Stop"));
            break;
        case OBS_FRONTEND_EVENT_EXIT:
            // Final cleanup opportunity
            break;
        // handle other events here
    }
}
```

### WebSocket Vendor API

```cpp
// Register in obs_module_post_load (after obs-websocket loads)
void obs_module_post_load(void) {
    obs_websocket_vendor vendor = obs_websocket_register_vendor("my-plugin");
    if (vendor) {
        g_dock->setVendor(vendor);
    }
}

// Emit events
obs_data_t *d = obs_data_create();
obs_data_set_string(d, "event", "started");
obs_data_set_double(d, "timeMs", 1234.0);
obs_websocket_vendor_emit_event(m_vendor, "MyEvent", d);
obs_data_release(d);

// Handle requests
obs_websocket_vendor_register_request(m_vendor, "GetStatus", requestCallback, this);
```

### Source/Scene Reference Rules

| API Call | Requires | Cleanup |
|----------|----------|---------|
| `obs_get_source_by_name()` | `obs_source_release()` | Always |
| `obs_frontend_get_current_scene()` | `obs_source_release()` | Always |
| `obs_frontend_get_scenes()` | `obs_frontend_source_list_free()` | Always |
| `obs_frontend_get_transitions()` | `obs_frontend_source_list_free()` | Always |
| `obs_source_get_filter_by_name()` | `obs_source_release()` | Always |

## Reference Files

- **[references/dock-plugins.md](references/dock-plugins.md)** — Dock lifecycle, callbacks, hotkeys, Qt threading
- **[references/websocket-api.md](references/websocket-api.md)** — Vendor registration, events, requests, auth
- **[references/settings-data.md](references/settings-data.md)** — obs_data_t, config paths, save/load patterns

## Verification Checklist

- [ ] `obs_module_unload` cleans up all global Qt objects
- [ ] Every `obs_frontend_add_*_callback` has matching `obs_frontend_remove_*_callback`
- [ ] Every `obs_hotkey_register_frontend` has matching `obs_hotkey_unregister`
- [ ] Every `obs_get_source_by_name` / `obs_frontend_get_current_scene` has `obs_source_release`
- [ ] Every `obs_frontend_get_scenes/transitions` has `obs_frontend_source_list_free`
- [ ] Every `obs_data_create` has `obs_data_release`
- [ ] Every `obs_module_get_config_path` return is freed with `bfree`
- [ ] No `pthread_*` usage (use Qt signals or `QMetaObject::invokeMethod`)
- [ ] No GPU operations (`gs_*` calls) in frontend code
- [ ] Hotkey callbacks check `pressed` before acting

## Testing Frontend Plugins

### Headless Test Setup (GL Context Required)

OBS graphics initialization requires a valid OpenGL context even in unit tests. Without it, you'll see:
```
debug: gs_texture_destroy: called while not in a graphics context
warning: device_get_size (GL): No active swap
```

**Fix:** Create an offscreen GL context *before* `obs_startup()`:

```cpp
#include <QGuiApplication>
#include <QOffscreenSurface>
#include <QOpenGLContext>

class TestTimeline : public QObject {
    Q_OFFSCREENSURFACE *offscreenSurface = nullptr;
    QOpenGLContext *glContext = nullptr;

    void initTestCase() {
        // Create headless GL context FIRST
        offscreenSurface = new QOffscreenSurface();
        offscreenSurface->setFormat(QSurfaceFormat::defaultFormat());
        offscreenSurface->create();

        glContext = new QOpenGLContext();
        glContext->setFormat(offscreenSurface->format());
        glContext->create();
        glContext->makeCurrent(offscreenSurface);  // Must be current before obs_startup

        obs_startup("en-US", nullptr, nullptr);
        // register the required sources
    }

    void cleanupTestCase() {
        // Re-make context current so texture cleanup happens on valid GL
        if (glContext && offscreenSurface) {
            glContext->makeCurrent(offscreenSurface);
        }
        obs_shutdown();

        glContext->doneCurrent();
        delete glContext;
        delete offscreenSurface;
    }
};
```

**Requirements:**
- Use `QGuiApplication` (not `QCoreApplication`)
- Link `Qt6::Gui` in test `CMakeLists.txt`
- Call `glContext->makeCurrent()` before `obs_shutdown()` to avoid cleanup warnings

---

### Missing Frontend API Mocks

Tests linking `src/obs-executor.cpp` need support functions for all `obs_frontend_*` functions, even if unused. Common missing ones:

```cpp
// tests/frontend_support.cpp
extern "C" {

void obs_frontend_get_scenes(struct obs_frontend_source_list *list) {
    if (!list) return;
    list->sources.num = 0;
    list->sources.array = nullptr;
    // If you need to populate scenes, allocate with bmalloc and addref each source
}

void obs_frontend_get_transitions(struct obs_frontend_source_list *list) {
    if (!list) return;
    list->sources.num = g_transitions.size();
    list->sources.array = (obs_source_t**)bmalloc(sizeof(obs_source_t*) * g_transitions.size());
    for (size_t i = 0; i < g_transitions.size(); i++) {
        list->sources.array[i] = g_transitions[i];
        obs_source_get_ref(g_transitions[i]);
    }
}

obs_source_t* obs_frontend_get_current_scene(void) {
    if (g_current_scene) obs_source_get_ref(g_current_scene);
    return g_current_scene;
}

// add other required frontend functions here
}
```

**Tip:** Check linker errors for exact missing symbols. Add a minimal function returning null or empty data.

---

## Advanced UI Patterns

### Settings Menu with Embedded Widgets

When your dock has too many controls for the toolbar, use a `QMenu` with `QWidgetAction` to embed any Qt widget:

```cpp
// In setupUi():
settingsButton = new QPushButton("Settings", this);
settingsMenu = new QMenu(settingsButton);

// Embed QCheckBox:
QWidgetAction *centerAction = new QWidgetAction(settingsMenu);
centerAction->setDefaultWidget(centerControlsCheckbox);
settingsMenu->addAction(centerAction);

// Embed QComboBox:
QWidgetAction *gridTypeAction = new QWidgetAction(settingsMenu);
gridTypeAction->setDefaultWidget(gridTypeCombo);
settingsMenu->addAction(gridTypeAction);

// Embed QSpinBox with label:
QWidgetAction *bpmAction = new QWidgetAction(settingsMenu);
bpmAction->setDefaultWidget(bpmSpinbox);
settingsMenu->addAction(bpmAction);

// Connect button to show menu:
connect(settingsButton, &QPushButton::clicked, this, [this]() {
    settingsMenu->exec(settingsButton->mapToGlobal(QPoint(0, settingsButton->height())));
});

// In updateControlsLayout(): remove embedded widgets from main layout
// Only add settingsButton to rightWidgets list
```

**Gotcha:** The embedded widgets must have a parent (usually `settingsMenu` or the main dock). They are automatically destroyed when the menu is destroyed. If you conditionally hide/show based on other settings (e.g., only show `noteLengthCombo` in BPM mode), call `setVisible()` in your mode-change handler.

---

### Two-Mode Grid with Note Subdivision

Separate time-based snapping from BPM-based musical snapping using a combo box and a note-length selector:

```cpp
// Members in header:
QComboBox *gridTypeCombo;     // "Time" (0) or "BPM" (1)
QComboBox *noteLengthCombo;   // Whole, Half, Quarter, Eighth, Sixteenth, 32nd
double noteLengthBeats = 1.0;  // Quarter note = 1 beat
bool gridToBpm = false;

// In setupUi():
gridTypeCombo = new QComboBox(this);
gridTypeCombo->addItem("Time", 0);
gridTypeCombo->addItem("BPM", 1);
connect(gridTypeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
        this, &TimelineDock::onGridModeChanged);

noteLengthCombo = new QComboBox(this);
noteLengthCombo->addItem("Whole", 4.0);
noteLengthCombo->addItem("Half", 2.0);
noteLengthCombo->addItem("Quarter", 1.0);
noteLengthCombo->addItem("Eighth", 0.5);
noteLengthCombo->addItem("Sixteenth", 0.25);
noteLengthCombo->addItem("32nd", 0.125);
connect(noteLengthCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
        this, &TimelineDock::onNoteLengthChanged);

// In onGridModeChanged:
void TimelineDock::onGridModeChanged(int index) {
    gridToBpm = (index == 1);
    noteLengthCombo->setVisible(gridToBpm);           // Show only in BPM mode
    updateControlsLayout();                          // Refresh main toolbar
    applySnapIntervalToItems();
    drawGrid();
}

// In getSnapIntervalMs():
double getSnapIntervalMs() const {
    if (gridToBpm && bpm > 0.0) {
        return (60.0 / bpm) * noteLengthBeats * 1000.0;
    }
    return manualSnapIntervalMs;  // from spinbox value
}

// In drawGrid() - calculate step based on mode:
if (gridToBpm && bpm > 0.0) {
    double beatIntervalSec = (60.0 / bpm) * noteLengthBeats;
    stepPx = beatIntervalSec * pixelsPerSecond;
} else {
    // calculate the adaptive time-based step here
}
```

**Persistence:** Save `gridToBpm` (bool or combo index) and `noteLengthBeats` (double) in your JSON settings. On load, restore with `noteLengthCombo->setCurrentIndex(noteLengthCombo->findData(noteLengthBeats))`.

---

## Robust UI Logic

### Re-queuing UI Rebuilds during User Interaction

Complex UI rebuilds (e.g., clearing and recreating many `QGraphicsItem`s) can cause crashes or visual glitches if they fire while the user is still interacting with an item (like dragging). Use a re-queuing guard:

```cpp
void MyDock::rebuildUI() {
    rebuildScheduled = false;

    // If user is dragging, defer the rebuild until they release
    if (isDragging) {
        rebuildScheduled = true;
        QMetaObject::invokeMethod(this, &MyDock::rebuildUI, Qt::QueuedConnection);
        return;
    }

    // Perform actual heavy lifting here...
}
```

### Unified Theme Handling

OBS triggers both `OBS_FRONTEND_EVENT_THEME_CHANGED` and a standard Qt `PaletteChange` event. To avoid double-repaints and redundant icon reconstruction:

1.  Handle business logic (colors, icons) in the **frontend event callback**.
2.  Handle viewport/widget updates in `changeEvent`.

```cpp
// In changeEvent:
void MyDock::changeEvent(QEvent *event) {
    QWidget::changeEvent(event);
    if (event->type() == QEvent::PaletteChange) {
        // Only update standard widget drawing; leave icons/logic to THEME_CHANGED
        if (view && view->viewport()) view->viewport()->update();
        update();
    }
}

// In THEME_CHANGED callback:
dock->ensurePolished();
dock->refreshCustomColors();
dock->reconstructIcons();
dock->updateLayout();
```

---

## Common Mistakes

| Symptom | Likely Cause | Fix |
|---|---|---|
| GL warnings in tests (`gs_texture_destroy: not in graphics context`) | No GL context before `obs_startup()` | Create `QOffscreenSurface` + `QOpenGLContext` in `initTestCase()` |
| Linker error: `undefined reference to obs_frontend_get_scenes` | Missing support function in the test target | Add a minimal function returning an empty list (see above) |
| Settings widgets disappear from menu after toggling | Parent ownership incorrect | Set parent to `settingsMenu` or `this`; use `QWidgetAction::setDefaultWidget` |
| Snap interval doesn't update when BPM changes | `getSnapIntervalMs()` ignores `noteLengthBeats` | Multiply by `noteLengthBeats` in BPM mode |
| UI shows both noteLengthCombo AND time interval in Time mode | `updateControlsLayout()` doesn't hide them | Conditionally append to `rightWidgets` only when `!gridToBpm` |

---

## Testing Strategy

1. **GL Context:** Verify no `device_get_size` warnings in test output.
2. **Callbacks:** Save/load, hotkeys, frontend events all unregister on unload.
3. **Serialization:** Round-trip JSON preserves grid mode, note length, BPM, snap interval.
4. **UI State:** Switching grid mode shows/hides appropriate controls; noteLengthCombo visible only in BPM mode.
5. **Linking:** All `obs_frontend_*` symbols provided by the test target; no undefined references.

Run: `cmake --build build && cd build && ./tests/run_tests`

**Grep for warnings:**
```bash
./tests/run_tests 2>&1 | grep -E "No active swap|device_get_size|undefined reference"
```
