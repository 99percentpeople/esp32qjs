#pragma once
#include "esp32_mquickjs_types.h"
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI && CONFIG_SOC_WIFI_HE_SUPPORT && CONFIG_IDF_TARGET_ESP32C5
JSValue js_wifi_twt_get_config(JSContext *, JSValue *, int, JSValue *);
JSValue js_wifi_twt_configure(JSContext *, JSValue *, int, JSValue *);
JSValue js_wifi_twt_get_flow_status(JSContext *, JSValue *, int, JSValue *);
JSValue js_wifi_twt_set_target_wake_time_offset(JSContext *, JSValue *, int, JSValue *);
bool esp32_mquickjs_init_wifi_twt_recovery_runtime(JSContext *, esp32_mquickjs_runtime_t *);
JSValue js_wifi_twt_recover(JSContext *, JSValue *, int, JSValue *);
bool esp32_mquickjs_init_wifi_twt_runtime(JSContext *, esp32_mquickjs_runtime_t *);
bool esp32_mquickjs_init_wifi_twt_broadcast_runtime(JSContext *, esp32_mquickjs_runtime_t *, JSValue *);
bool esp32_mquickjs_init_wifi_twt_close_runtime(JSContext *, esp32_mquickjs_runtime_t *, JSValue *);
JSValue js_wifi_twt_close_all(JSContext *, JSValue *, int, JSValue *);
JSValue js_wifi_twt_broadcasts(JSContext *, JSValue *, int, JSValue *);
bool esp32_mquickjs_prepare_wifi_twt_runtime_destroy(void);
bool esp32_mquickjs_wifi_twt_service(void);
bool esp32_mquickjs_init_wifi_twt_agreement_runtime(JSContext *, esp32_mquickjs_runtime_t *);
bool esp32_mquickjs_wifi_twt_agreement_service(void);
bool esp32_mquickjs_prepare_wifi_twt_agreement_runtime_destroy(void);
JSValue js_wifi_twt_setup_broadcast(JSContext *, JSValue *, int, JSValue *);
JSValue js_wifi_twt_setup_individual(JSContext *, JSValue *, int, JSValue *);
JSValue js_wifi_twt_agreements(JSContext *, JSValue *, int, JSValue *);
JSValue js_wifi_twt_agreement_constructor(JSContext *, JSValue *, int, JSValue *);
JSValue js_wifi_twt_agreement_status(JSContext *, JSValue *, int, JSValue *);
JSValue js_wifi_twt_agreement_close(JSContext *, JSValue *, int, JSValue *);
JSValue js_wifi_twt_agreement_suspend(JSContext *, JSValue *, int, JSValue *);
JSValue js_wifi_twt_agreement_resume(JSContext *, JSValue *, int, JSValue *);
void js_wifi_twt_agreement_finalizer(JSContext *, void *);
JSValue js_wifi_twt_probe(JSContext *, JSValue *, int, JSValue *);
JSValue js_wifi_twt_status(JSContext *, JSValue *, int, JSValue *);
JSValue js_wifi_twt_capabilities(JSContext *, JSValue *, int, JSValue *);
#endif
