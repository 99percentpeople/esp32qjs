#pragma once
#include "sdkconfig.h"
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI && CONFIG_ESP_WIFI_WAPI_PSK
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "mquickjs.h"

typedef struct {
    uint32_t revision, generation;
    bool requested_enabled, supplicant_active, enabled, busy, uncertain;
    esp_err_t error, cleanup_error;
} esp32_mquickjs_wifi_wapi_status_t;
void esp32_mquickjs_wifi_wapi_sdk_status(esp32_mquickjs_wifi_wapi_status_t *out);
/* Radio serializes policy writes against initialization. Changing this value
 * never calls native WAPI init/deinit outside supplicant ownership. */
esp_err_t esp32_mquickjs_wifi_wapi_sdk_policy(bool enabled);
esp_err_t esp32_mquickjs_wifi_wapi_sdk_cleanup_error(void);
bool esp32_mquickjs_wifi_capture_wapi_control(JSContext *, JSValue, bool, uint32_t *);
JSValue js_wifi_wapi_capabilities(JSContext *, JSValue *, int, JSValue *);
JSValue js_wifi_wapi_status(JSContext *, JSValue *, int, JSValue *);
JSValue js_wifi_wapi_enable(JSContext *, JSValue *, int, JSValue *);
JSValue js_wifi_wapi_disable(JSContext *, JSValue *, int, JSValue *);
#endif
