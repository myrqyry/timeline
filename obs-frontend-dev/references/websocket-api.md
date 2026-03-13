# WebSocket Vendor API Reference

## Overview

The obs-websocket vendor API lets plugins emit events and handle requests through the OBS WebSocket server. External clients (e.g., StreamDeck, Touch Portal) can subscribe to your events and call your request handlers.

## Prerequisites

- obs-websocket plugin must be loaded (OBS bundles it since v28)
- Register in `obs_module_post_load()`, not `obs_module_load()`

## Vendor Registration

```cpp
void obs_module_post_load(void) {
    obs_websocket_vendor vendor = obs_websocket_register_vendor("my-plugin");
    if (vendor) {
        g_dock->setVendor(vendor);
    } else {
        blog(LOG_WARNING, "obs-websocket not available");
    }
}
```

The vendor pointer is an opaque `void*` — store it globally or in your main widget.

## Emitting Events

Emit events to notify WebSocket clients of state changes:

```cpp
void MyDock::emitPlaybackEvent(const char *state, double timeMs) {
    if (!m_vendor) return;

    obs_data_t *d = obs_data_create();
    obs_data_set_string(d, "event", state);     // "started", "paused", "stopped"
    obs_data_set_double(d, "timeMs", timeMs);
    obs_data_set_string(d, "source", "timeline");

    obs_websocket_vendor_emit_event(m_vendor, "PlaybackStatus", d);
    obs_data_release(d);  // REQUIRED
}
```

### Rate Limiting

Don't emit on every tick. Throttle high-frequency events:

```cpp
void MyDock::onTick(double timeMs) {
    qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (now - m_lastEmitTime > 50) {  // Max ~20 events/sec
        m_lastEmitTime = now;
        emitPlaybackEvent("tick", timeMs);
    }
}
```

## Handling Requests

Register request handlers for external tools to query/modify state:

```cpp
void MyDock::registerRequests() {
    if (!m_vendor) return;

    obs_websocket_vendor_register_request(m_vendor, "GetStatus",
        onRequestGetStatus, this);
    obs_websocket_vendor_register_request(m_vendor, "Play",
        onRequestPlay, this);
}

static void onRequestGetStatus(void *priv_data,
                                obs_data_t *request_data,
                                obs_data_t *response_data) {
    MyDock *dock = static_cast<MyDock*>(priv_data);

    obs_data_set_bool(response_data, "playing", dock->isPlaying());
    obs_data_set_double(response_data, "currentTimeMs", dock->currentTime());
    obs_data_set_string(response_data, "status", "ok");
}
```

## Event Naming Conventions

| Event Name | Payload Fields | When |
|------------|---------------|------|
| `PlaybackStatus` | `event`, `timeMs` | Play/pause/stop/loop |
| `TimelineEvent` | `event`, `type`, `sourceName`, `timeMs` | Event triggered |
| `StateChange` | `state`, `details` | Plugin state changes |

## obs_data_t Memory

```cpp
// Always pair create/release
obs_data_t *d = obs_data_create();
// ... set fields ...
obs_websocket_vendor_emit_event(vendor, "EventName", d);
obs_data_release(d);  // REQUIRED — leaks if skipped
```

## Client Example

WebSocket clients connect and subscribe:

```json
// Request
{
  "op": 6,
  "d": {
    "requestType": "GetStatus",
    "requestId": "...",
    "requestData": {}
  }
}

// Response
{
  "op": 7,
  "d": {
    "requestId": "...",
    "requestStatus": { "result": true, "code": 100 },
    "responseData": {
      "playing": true,
      "currentTimeMs": 5432.1
    }
  }
}
```
