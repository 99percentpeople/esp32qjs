#pragma once
#include "esp32_mquickjs_wifi_vendor_ie.h"
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI
/* Only the Radio mutation owner may register, unregister or reset. Callback
 * context is a non-reused generation value, never a runtime/queue pointer. */
typedef struct {
    uint32_t generation, callbacks_active;
    bool registered, uncertain, unregister_written, control_busy, accepting;
    esp_err_t error;
} esp32_mquickjs_wifi_vendor_ie_broker_status_t;
esp_err_t esp32_mquickjs_wifi_vendor_ie_broker_register(uint32_t generation);
esp_err_t esp32_mquickjs_wifi_vendor_ie_broker_unregister(uint32_t generation);
bool esp32_mquickjs_wifi_vendor_ie_broker_reset(uint32_t generation);
void esp32_mquickjs_wifi_vendor_ie_broker_status(esp32_mquickjs_wifi_vendor_ie_broker_status_t *status);
void esp32_mquickjs_wifi_vendor_ie_watch_capture(uint32_t generation, wifi_vendor_ie_type_t frame,
    const uint8_t address[6], const vendor_ie_data_t *data, int rssi);
void esp32_mquickjs_deinit_wifi_vendor_ie_watch_runtime(void);
JSValue esp32_mquickjs_wifi_vendor_ie_watch_status(JSContext *ctx);
JSValue js_wifi_vendor_ie_watch(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
#endif
