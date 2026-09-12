#pragma once
#include "esp32_mquickjs_types.h"
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI
bool esp32_mquickjs_init_wifi_action_runtime(JSContext *, esp32_mquickjs_runtime_t *);
bool esp32_mquickjs_prepare_wifi_action_runtime_destroy(void);
JSValue esp32_mquickjs_wifi_action_status(JSContext *);
bool esp32_mquickjs_init_wifi_action_recovery_runtime(JSContext *, esp32_mquickjs_runtime_t *);
JSValue js_wifi_action_recover(JSContext *, JSValue *, int, JSValue *);
JSValue js_wifi_action_send(JSContext *, JSValue *, int, JSValue *);
JSValue js_wifi_action_status(JSContext *, JSValue *, int, JSValue *);
JSValue js_wifi_action_capabilities(JSContext *, JSValue *, int, JSValue *);
bool esp32_mquickjs_init_wifi_roc_runtime(JSContext *, esp32_mquickjs_runtime_t *);
JSValue js_wifi_action_remain_on_channel(JSContext *, JSValue *, int, JSValue *);
JSValue js_wifi_roc_constructor(JSContext *, JSValue *, int, JSValue *);
void js_wifi_roc_finalizer(JSContext *, void *);
JSValue js_wifi_roc_status(JSContext *, JSValue *, int, JSValue *);
JSValue js_wifi_roc_wait(JSContext *, JSValue *, int, JSValue *);
JSValue js_wifi_roc_close(JSContext *, JSValue *, int, JSValue *);
#endif
