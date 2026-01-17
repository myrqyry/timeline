#pragma once

#include <obs.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void *obs_websocket_vendor;

typedef void (*obs_websocket_vendor_request_callback)(void *priv_data, obs_data_t *request_data, obs_data_t *response_data);

EXPORT obs_websocket_vendor obs_websocket_register_vendor(const char *vendor_name);

EXPORT void obs_websocket_vendor_register_request(obs_websocket_vendor vendor, const char *request_type,
                                                  obs_websocket_vendor_request_callback callback, void *priv_data);

EXPORT void obs_websocket_vendor_emit_event(obs_websocket_vendor vendor, const char *event_name, obs_data_t *event_data);

#ifdef __cplusplus
}
#endif
