#pragma once
#include "esp32_mquickjs_types.h"
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI && CONFIG_ESP_WIFI_FTM_ENABLE && CONFIG_ESP_WIFI_FTM_INITIATOR_SUPPORT
bool esp32_mquickjs_init_wifi_ftm_runtime(JSContext *, esp32_mquickjs_runtime_t *);
bool esp32_mquickjs_init_wifi_ftm_recovery_runtime(JSContext *, esp32_mquickjs_runtime_t *);
JSValue js_wifi_ftm_recover(JSContext *, JSValue *, int, JSValue *);
JSValue js_wifi_ftm_start(JSContext *, JSValue *, int, JSValue *);
JSValue js_wifi_ftm_global_status(JSContext *, JSValue *, int, JSValue *);
JSValue js_wifi_ftm_constructor(JSContext *, JSValue *, int, JSValue *);
void js_wifi_ftm_finalizer(JSContext *, void *);
JSValue js_wifi_ftm_status(JSContext *, JSValue *, int, JSValue *);
JSValue js_wifi_ftm_receive(JSContext *, JSValue *, int, JSValue *);
JSValue js_wifi_ftm_end(JSContext *, JSValue *, int, JSValue *);
JSValue js_wifi_ftm_close(JSContext *, JSValue *, int, JSValue *);
#endif
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI && CONFIG_ESP_WIFI_FTM_ENABLE && (CONFIG_ESP_WIFI_FTM_INITIATOR_SUPPORT || (CONFIG_ESP_WIFI_FTM_RESPONDER_SUPPORT && CONFIG_ESP_WIFI_SOFTAP_SUPPORT))
JSValue js_wifi_ftm_capabilities(JSContext *, JSValue *, int, JSValue *);
#endif
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI && CONFIG_ESP_WIFI_FTM_ENABLE && CONFIG_ESP_WIFI_FTM_RESPONDER_SUPPORT && CONFIG_ESP_WIFI_SOFTAP_SUPPORT
JSValue js_wifi_ftm_set_responder_offset(JSContext *, JSValue *, int, JSValue *);
JSValue js_wifi_ftm_responder_offset_status(JSContext *, JSValue *, int, JSValue *);
#endif
