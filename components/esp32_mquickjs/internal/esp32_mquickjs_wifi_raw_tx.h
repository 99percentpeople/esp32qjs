#pragma once
#include "esp32_mquickjs_types.h"
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI
#include "esp32_mquickjs_wifi_raw_tx_broker.h"
#include "esp32_mquickjs_wifi_raw_tx_session.h"
/* Shared ByteSource capture: empty outputs required. Any allocated bytes remain
 * caller-owned on success OR failure and must be freed by the caller. No SDK I/O.
 * value is a live GC root; operation must be a static diagnostic name. */
bool esp32_mquickjs_wifi_raw_tx_capture_bytes(JSContext *, JSGCRef *value,
    const char *operation, uint8_t **bytes, size_t *length);
JSValue esp32_mquickjs_wifi_raw_tx_result_to_js(JSContext *,
    const esp32_mquickjs_wifi_raw_tx_broker_status_t *, esp32_mquickjs_wifi_raw_tx_interface_t, uint8_t channel);
JSValue js_wifi_raw_tx_recover(JSContext *, JSValue *, int, JSValue *);
bool esp32_mquickjs_init_wifi_raw_tx_recovery_runtime(JSContext *, esp32_mquickjs_runtime_t *);
bool esp32_mquickjs_init_wifi_raw_tx_runtime(JSContext *, esp32_mquickjs_runtime_t *);
bool esp32_mquickjs_init_wifi_raw_tx_session_runtime(JSContext *, esp32_mquickjs_runtime_t *);
bool esp32_mquickjs_prepare_wifi_raw_tx_runtime_destroy(void);
bool esp32_mquickjs_wifi_raw_tx_session_capture_owner(JSContext *, JSValue,
    esp32_mquickjs_wifi_raw_tx_session_t **, esp32_mquickjs_wifi_raw_tx_session_options_t *);
bool esp32_mquickjs_init_wifi_raw_tx_periodic_runtime(JSContext *, esp32_mquickjs_runtime_t *);
JSValue js_wifi_raw_tx_start_periodic(JSContext *, JSValue *, int, JSValue *);
JSValue js_wifi_raw_periodic_constructor(JSContext *, JSValue *, int, JSValue *);
void js_wifi_raw_periodic_finalizer(JSContext *, void *);
JSValue js_wifi_raw_periodic_status(JSContext *, JSValue *, int, JSValue *);
JSValue js_wifi_raw_periodic_stop(JSContext *, JSValue *, int, JSValue *);
JSValue js_wifi_raw_periodic_close(JSContext *, JSValue *, int, JSValue *);
JSValue esp32_mquickjs_wifi_raw_tx_status(JSContext *);
JSValue js_wifi_raw_tx_send(JSContext *, JSValue *, int, JSValue *);
JSValue js_wifi_raw_tx_capabilities(JSContext *, JSValue *, int, JSValue *);
JSValue js_wifi_raw_tx_open(JSContext *, JSValue *, int, JSValue *);
JSValue js_wifi_raw_tx_session_constructor(JSContext *, JSValue *, int, JSValue *);
void js_wifi_raw_tx_session_finalizer(JSContext *, void *);
JSValue js_wifi_raw_tx_session_send(JSContext *, JSValue *, int, JSValue *);
JSValue js_wifi_raw_tx_session_enqueue(JSContext *, JSValue *, int, JSValue *);
JSValue js_wifi_raw_tx_session_enqueue_batch(JSContext *, JSValue *, int, JSValue *);
JSValue js_wifi_raw_tx_session_wait_writable(JSContext *, JSValue *, int, JSValue *);
JSValue js_wifi_raw_tx_session_flush(JSContext *, JSValue *, int, JSValue *);
JSValue js_wifi_raw_tx_session_status(JSContext *, JSValue *, int, JSValue *);
JSValue js_wifi_raw_tx_session_stats(JSContext *, JSValue *, int, JSValue *);
JSValue js_wifi_raw_tx_session_close(JSContext *, JSValue *, int, JSValue *);
#endif
