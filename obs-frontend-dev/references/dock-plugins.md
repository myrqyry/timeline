# Dock Plugin Reference

## Lifecycle

```
obs_module_load()          → Create widget, register dock
  ↓
obs_module_post_load()     → Register WebSocket vendor (after obs-websocket loads)
  ↓
[Plugin running]           → Callbacks, events, hotkeys active
  ↓
obs_module_unload()        → Clean up globals
```

### Constructor Pattern

```cpp
MyDock::MyDock(QWidget *parent) : QWidget(parent) {
    // 1. Create model/scheduler/executor
    m_model = new TimelineModel(this);
    m_scheduler = new TimelineScheduler(m_model, this);
    m_executor = new ObsExecutor(this);

    // 2. Setup UI
    setupUi();

    // 3. Connect Qt signals
    connect(m_scheduler, &Scheduler::tick, this, &MyDock::onTick);

    // 4. Register OBS callbacks (paired with removal in destructor)
    obs_frontend_add_save_callback(saveCallback, this);
    obs_frontend_add_event_callback(frontendEvent, this);

    // 5. Register hotkeys
    m_hotkeyPlay = obs_hotkey_register_frontend(
        "My.Play", "My Plugin: Play", hotkeyPlay, this);

    // 6. Load saved data
    load();
}
```

### Destructor Pattern

```cpp
MyDock::~MyDock() {
    // Unregister in reverse order
    obs_frontend_remove_save_callback(saveCallback, this);
    obs_frontend_remove_event_callback(frontendEvent, this);
    obs_hotkey_unregister(m_hotkeyPlay);
    obs_hotkey_unregister(m_hotkeyStop);
}
```

## Callback Registration

| Register | Unregister | Purpose |
|----------|------------|---------|
| `obs_frontend_add_save_callback` | `obs_frontend_remove_save_callback` | Persist settings |
| `obs_frontend_add_event_callback` | `obs_frontend_remove_event_callback` | React to OBS events |
| `obs_hotkey_register_frontend` | `obs_hotkey_unregister` | Global hotkeys |

## Hotkey Pattern

```cpp
// Static callback — runs on whatever thread triggered the hotkey
static void hotkeyPlay(void *data, obs_hotkey_id id, obs_hotkey_t *hotkey, bool pressed) {
    if (!pressed) return;  // Only act on key down
    MyDock *dock = static_cast<MyDock*>(data);
    QMetaObject::invokeMethod(dock, "onPlayClicked", Qt::QueuedConnection);
}
```

## Qt Threading

### Safe Patterns

```cpp
// ✅ Queued connection to slot (cross-thread safe)
QMetaObject::invokeMethod(dock, "methodName", Qt::QueuedConnection);

// ✅ With arguments
QMetaObject::invokeMethod(dock->button, "setText", Qt::QueuedConnection,
    Q_ARG(QString, "Stop"));

// ✅ Qt signals/slots (auto-queued across threads)
connect(sender, &SenderClass::signal, receiver, &ReceiverClass::slot);
```

### Unsafe Patterns

```cpp
// ❌ Direct UI update from non-UI thread
void hotkeyCallback(void *data, ...) {
    MyDock *dock = static_cast<MyDock*>(data);
    dock->button->setText("Stop");  // WRONG THREAD
}

// ❌ Direct pthread usage
pthread_mutex_lock(&mutex);  // Use Qt signals or std::atomic
```

## Frontend Events

Key events for dock plugins:

| Event | When |
|-------|------|
| `OBS_FRONTEND_EVENT_RECORDING_STARTED` | Recording begins |
| `OBS_FRONTEND_EVENT_RECORDING_STOPPED` | Recording ends |
| `OBS_FRONTEND_EVENT_STREAMING_STARTED` | Stream begins |
| `OBS_FRONTEND_EVENT_STREAMING_STOPPED` | Stream ends |
| `OBS_FRONTEND_EVENT_SCENE_CHANGED` | Active scene changed |
| `OBS_FRONTEND_EVENT_EXIT` | OBS shutting down — final cleanup |

## Scene/Source Enumeration

```cpp
// List all scenes
struct obs_frontend_source_list scenes = {};
obs_frontend_get_scenes(&scenes);
for (size_t i = 0; i < scenes.sources.num; i++) {
    const char *name = obs_source_get_name(scenes.sources.array[i]);
}
obs_frontend_source_list_free(&scenes);  // REQUIRED

// Get items in current scene
obs_source_t *scene_src = obs_frontend_get_current_scene();
if (scene_src) {
    obs_scene_t *scene = obs_scene_from_source(scene_src);
    obs_scene_enum_items(scene, [](obs_scene_t*, obs_sceneitem_t *item, void *param) {
        obs_source_t *src = obs_sceneitem_get_source(item);
        const char *name = obs_source_get_name(src);
        return true;  // Continue enumeration
    }, nullptr);
    obs_source_release(source_src);  // REQUIRED
}

// Get filters on a source
obs_source_enum_filters(source, [](obs_source_t*, obs_source_t *filter, void *data) {
    /* Handle filter logic here */
    // Note: callback returns void, not bool
}, userData);
```

---

## Performance & Optimization

### Debounced Persistence

Frequent UI actions (like dragging an item or typing a label) should not trigger immediate disk writes. Use a single-shot `QTimer` to debounce saves:

```cpp
// In constructor:
m_saveTimer = new QTimer(this);
m_saveTimer->setSingleShot(true);
m_saveTimer->setInterval(250); // Coalesce changes within 250ms
connect(m_saveTimer, &QTimer::timeout, this, &MyDock::save);

// In model change handler:
m_saveTimer->start();
```

### Scene/Source Search Efficiency

When searching for a source in multiple scenes, stop searching as soon as the first match is found and acted upon to avoid unintended side-effects in other scenes.

```cpp
bool found = false;
obs_frontend_get_scenes(&list);
for (size_t i = 0; i < list.sources.num; i++) {
    if (tryActionOnSource(list.sources.array[i])) {
        found = true;
        break; // Stop at first match
    }
}
obs_frontend_source_list_free(&list);
```
