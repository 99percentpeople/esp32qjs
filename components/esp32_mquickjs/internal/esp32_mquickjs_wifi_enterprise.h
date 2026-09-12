#pragma once
#include "esp32_mquickjs_types.h"
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI && CONFIG_ESP_WIFI_ENTERPRISE_SUPPORT
JSValue js_wifi_enterprise_capabilities(JSContext *, JSValue *, int, JSValue *);
JSValue js_wifi_enterprise_configure(JSContext *, JSValue *, int, JSValue *);
JSValue js_wifi_enterprise_status(JSContext *, JSValue *, int, JSValue *);
JSValue js_wifi_enterprise_enable(JSContext *, JSValue *, int, JSValue *);
JSValue js_wifi_enterprise_disable(JSContext *, JSValue *, int, JSValue *);
JSValue js_wifi_enterprise_clear(JSContext *, JSValue *, int, JSValue *);
bool esp32_mquickjs_init_wifi_enterprise_runtime(JSContext *, esp32_mquickjs_runtime_t *);
#endif
