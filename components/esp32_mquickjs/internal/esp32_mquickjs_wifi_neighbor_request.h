#pragma once
#include "esp32_mquickjs_types.h"
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI && CONFIG_ESP_WIFI_RRM_SUPPORT
JSValue js_wifi_neighbor_request(JSContext *, JSValue *, int, JSValue *);
JSValue js_wifi_neighbor_constructor(JSContext *, JSValue *, int, JSValue *);
void js_wifi_neighbor_finalizer(JSContext *, void *);
JSValue js_wifi_neighbor_status(JSContext *, JSValue *, int, JSValue *);
JSValue js_wifi_neighbor_module_status(JSContext *, JSValue *, int, JSValue *);
JSValue js_wifi_neighbor_receive(JSContext *, JSValue *, int, JSValue *);
JSValue js_wifi_neighbor_cancel(JSContext *, JSValue *, int, JSValue *);
JSValue js_wifi_neighbor_close(JSContext *, JSValue *, int, JSValue *);
bool esp32_mquickjs_init_wifi_neighbor_runtime(JSContext *, esp32_mquickjs_runtime_t *);
#endif
