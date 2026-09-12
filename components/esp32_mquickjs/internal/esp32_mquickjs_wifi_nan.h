#pragma once
#include "esp32_mquickjs_types.h"
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI && (CONFIG_ESP_WIFI_NAN_SYNC_ENABLE || CONFIG_ESP_WIFI_NAN_USD_ENABLE)
#if CONFIG_ESP_WIFI_NAN_PAIRING
JSValue js_wifi_nan_pairing_credentials(JSContext *, JSValue *, int, JSValue *);
JSValue js_wifi_nan_pairing_request(JSContext *, JSValue *, int, JSValue *);
JSValue js_wifi_nan_pairing_receive(JSContext *, JSValue *, int, JSValue *);
JSValue js_wifi_nan_pairing_prepare(JSContext *, JSValue *, int, JSValue *);
JSValue js_wifi_nan_pairing_constructor(JSContext *, JSValue *, int, JSValue *);
void js_wifi_nan_pairing_finalizer(JSContext *, void *);
JSValue js_wifi_nan_pairing_status(JSContext *, JSValue *, int, JSValue *);
JSValue js_wifi_nan_pairing_confirm(JSContext *, JSValue *, int, JSValue *);
JSValue js_wifi_nan_pairing_ready(JSContext *, JSValue *, int, JSValue *);
JSValue js_wifi_nan_pairing_close(JSContext *, JSValue *, int, JSValue *);
JSValue js_wifi_nan_pairing_cancel(JSContext *, JSValue *, int, JSValue *);
#endif
bool esp32_mquickjs_init_wifi_nan_runtime(JSContext *, esp32_mquickjs_runtime_t *);
JSValue js_wifi_nan_capabilities(JSContext *, JSValue *, int, JSValue *);
JSValue js_wifi_nan_global_status(JSContext *, JSValue *, int, JSValue *);
JSValue js_wifi_nan_open(JSContext *, JSValue *, int, JSValue *);
JSValue js_wifi_nan_constructor(JSContext *, JSValue *, int, JSValue *);
void js_wifi_nan_finalizer(JSContext *, void *);
JSValue js_wifi_nan_status(JSContext *, JSValue *, int, JSValue *);
JSValue js_wifi_nan_ready(JSContext *, JSValue *, int, JSValue *);
JSValue js_wifi_nan_close(JSContext *, JSValue *, int, JSValue *);
JSValue js_wifi_nan_cancel(JSContext *, JSValue *, int, JSValue *);
JSValue js_wifi_nan_publish(JSContext *, JSValue *, int, JSValue *);
JSValue js_wifi_nan_subscribe(JSContext *, JSValue *, int, JSValue *);
JSValue js_wifi_nan_service_constructor(JSContext *, JSValue *, int, JSValue *);
void js_wifi_nan_service_finalizer(JSContext *, void *);
JSValue js_wifi_nan_service_status(JSContext *, JSValue *, int, JSValue *);
JSValue js_wifi_nan_service_ready(JSContext *, JSValue *, int, JSValue *);
JSValue js_wifi_nan_service_close(JSContext *, JSValue *, int, JSValue *);
JSValue js_wifi_nan_service_cancel(JSContext *, JSValue *, int, JSValue *);
JSValue js_wifi_nan_service_send(JSContext *, JSValue *, int, JSValue *);
#if CONFIG_ESP_WIFI_NAN_SYNC_ENABLE
JSValue js_wifi_nan_get_service_info(JSContext *, JSValue *, int, JSValue *);
JSValue js_wifi_nan_get_peer_info(JSContext *, JSValue *, int, JSValue *);
JSValue js_wifi_nan_get_peer_records(JSContext *, JSValue *, int, JSValue *);
JSValue js_wifi_nan_path_request(JSContext *, JSValue *, int, JSValue *);
JSValue js_wifi_nan_path_receive(JSContext *, JSValue *, int, JSValue *);
JSValue js_wifi_nan_path_constructor(JSContext *, JSValue *, int, JSValue *);
void js_wifi_nan_path_finalizer(JSContext *, void *);
JSValue js_wifi_nan_path_status(JSContext *, JSValue *, int, JSValue *);
JSValue js_wifi_nan_path_ready(JSContext *, JSValue *, int, JSValue *);
JSValue js_wifi_nan_path_close(JSContext *, JSValue *, int, JSValue *);
JSValue js_wifi_nan_path_cancel(JSContext *, JSValue *, int, JSValue *);
JSValue js_wifi_nan_path_respond(JSContext *, JSValue *, int, JSValue *);
#endif
#endif
