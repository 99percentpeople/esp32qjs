#pragma once
#include "esp32_mquickjs_types.h"
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI
#include "esp_wifi.h"
#define ESP32_MQUICKJS_WIFI_VENDOR_IE_SLOTS 10U
#define ESP32_MQUICKJS_WIFI_VENDOR_IE_MAX_BYTES 257U
/* No SDK getter: these describe framework-owned slots, never RF delivery. */
typedef struct {
    uint16_t length;
    bool pending;
    esp_err_t error;
} esp32_mquickjs_wifi_vendor_ie_slot_t;
typedef struct {
    uint32_t generation;
    uint32_t owners;
    bool start_pending;
    esp32_mquickjs_wifi_vendor_ie_slot_t slots[ESP32_MQUICKJS_WIFI_VENDOR_IE_SLOTS];
} esp32_mquickjs_wifi_vendor_ie_status_t;
/* Native stable input; caller captures JS before entering the Radio mutex. */
esp_err_t esp32_mquickjs_wifi_radio_vendor_ie_set(wifi_interface_t interface,
    wifi_vendor_ie_type_t frame, unsigned index, bool enabled, const uint8_t *bytes, size_t length);
/* interface=-1 clears both; failed slots alone remain owned for explicit retry. */
esp_err_t esp32_mquickjs_wifi_radio_vendor_ie_clear(int interface);
void esp32_mquickjs_wifi_radio_vendor_ie_status(esp32_mquickjs_wifi_vendor_ie_status_t *status);
JSValue js_wifi_vendor_ie_set(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_wifi_vendor_ie_clear(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_wifi_vendor_ie_status(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue esp32_mquickjs_wifi_vendor_ie_status(JSContext *ctx);
#endif
