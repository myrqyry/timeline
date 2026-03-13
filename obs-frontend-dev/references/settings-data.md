# Settings & Data Management Reference

## obs_data_t API

OBS uses `obs_data_t` as a JSON-like key-value store for settings, save data, and WebSocket messages.

### Lifecycle

```cpp
// Create → Use → Release
obs_data_t *data = obs_data_create();
obs_data_set_string(data, "key", "value");
obs_data_release(data);  // ALWAYS required
```

### Type-Safe Getters/Setters

| Type | Set | Get |
|------|-----|-----|
| String | `obs_data_set_string(d, "k", "v")` | `obs_data_get_string(d, "k")` |
| Int | `obs_data_set_int(d, "k", 42)` | `obs_data_get_int(d, "k")` |
| Double | `obs_data_set_double(d, "k", 3.14)` | `obs_data_get_double(d, "k")` |
| Bool | `obs_data_set_bool(d, "k", true)` | `obs_data_get_bool(d, "k")` |
| Object | `obs_data_set_obj(d, "k", child)` | `obs_data_get_obj(d, "k")` |
| Array | `obs_data_set_array(d, "k", arr)` | `obs_data_get_array(d, "k")` |

### Nested Objects

```cpp
obs_data_t *root = obs_data_create();
obs_data_t *settings = obs_data_create();

obs_data_set_bool(settings, "loop", true);
obs_data_set_double(settings, "endTime", 60.0);

obs_data_set_obj(root, "settings", settings);
obs_data_release(settings);  // Release child after embedding

// Reading
obs_data_t *s = obs_data_get_obj(root, "settings");
bool loop = obs_data_get_bool(s, "loop");
obs_data_release(s);  // Release returned object

obs_data_release(root);
```

## Config File Pattern

### Using obs_module_get_config_path

```cpp
// Get plugin-specific config directory
char *configPath = obs_module_get_config_path(obs_current_module(), "settings.json");
if (!configPath) return;

// Write
obs_data_t *data = obs_data_create();
obs_data_set_string(data, "key", "value");
obs_data_save_json(data, configPath);
obs_data_release(data);

bfree(configPath);  // REQUIRED — OBS-allocated string
```

### Save Callback Pattern

Register a save callback to persist data with the scene collection:

```cpp
// In constructor:
obs_frontend_add_save_callback(saveCallback, this);

// In destructor:
obs_frontend_remove_save_callback(saveCallback, this);

// Implementation:
static void saveCallback(obs_data_t *save_data, bool saving, void *priv_data) {
    MyDock *dock = static_cast<MyDock*>(priv_data);

    if (saving) {
        // Embed your settings into the save_data object
        obs_data_set_string(save_data, "my_key", "my_value");

        // For complex data, create a sub-object
        obs_data_t *settings = obs_data_create();
        obs_data_set_bool(settings, "loop", dock->isLooping());
        obs_data_set_double(settings, "endTime", dock->endTime());
        obs_data_set_obj(save_data, "my_settings", settings);
        obs_data_release(settings);
    } else {
        // Loading — read from save_data
        const char *val = obs_data_get_string(save_data, "my_key");

        obs_data_t *settings = obs_data_get_obj(save_data, "my_settings");
        if (settings) {
            bool loop = obs_data_get_bool(settings, "loop");
            obs_data_release(settings);
        }
    }
}
```

## JSON File Pattern (Qt)

For user-facing import/export (not tied to scene collection):

```cpp
// Export
void MyDock::exportToFile(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) return;

    QJsonObject root;
    root["events"] = model->toJson();

    QJsonObject settings;
    settings["loop"] = m_loopEnabled;
    settings["endTime"] = m_endTime;
    root["settings"] = settings;

    QJsonDocument doc(root);
    file.write(doc.toJson());
}

// Import
void MyDock::importFromFile(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return;

    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (doc.isObject()) {
        QJsonObject root = doc.object();
        model->fromJson(root["events"].toArray());

        QJsonObject s = root["settings"].toObject();
        m_loopEnabled = s["loop"].toBool();
        m_endTime = s["endTime"].toDouble();
    }
}
```

## Reference Counting Rules

| Object | Create | Release |
|--------|--------|---------|
| `obs_data_t` | `obs_data_create()` | `obs_data_release()` |
| `obs_data_t` (from get) | `obs_data_get_obj()` | `obs_data_release()` |
| String from OBS | `obs_module_get_config_path()` | `bfree()` |
| Source ref | `obs_get_source_by_name()` | `obs_source_release()` |

**Critical:** Mixing `bmalloc`/`bfree` with `malloc`/`free` crashes. Always use the matching pair.
