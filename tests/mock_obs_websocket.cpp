#include <obs-websocket-api.h>
#include <cstdio>

extern "C" {

obs_websocket_vendor obs_websocket_register_vendor(const char *vendor_name) {
    // Return a dummy pointer
    return (void*)0x12345678;
}

void obs_websocket_vendor_register_request(obs_websocket_vendor vendor, const char *request_type,
                                           obs_websocket_vendor_request_callback callback, void *priv_data) {
    // No-op
}

void obs_websocket_vendor_emit_event(obs_websocket_vendor vendor, const char *event_name, obs_data_t *event_data) {
    // No-op
}

}
