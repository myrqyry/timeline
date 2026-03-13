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
    // ... write to path ...
    bfree(path);  // REQUIRED
}

void MyDock::load() {
    char *path = obs_module_get_config_path(obs_current_module(), "config.json");
    if (!path) return;
    // ... read from path ...
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
        // ... other events
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
