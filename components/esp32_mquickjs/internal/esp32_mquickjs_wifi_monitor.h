#pragma once
#include "esp32_mquickjs_wifi_monitor_session.h"
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI
JSValue esp32_mquickjs_wifi_monitor_diagnostics(JSContext *ctx);
JSValue esp32_mquickjs_wifi_monitor_info_to_js(JSContext *ctx,
    const esp32_mquickjs_wifi_monitor_info_t *info, uint32_t sequence, uint32_t radio_generation);
JSValue js_wifi_monitor_capabilities(JSContext *, JSValue *, int, JSValue *);
JSValue js_wifi_monitor_open(JSContext *, JSValue *, int, JSValue *);
JSValue js_wifi_monitor_session_constructor(JSContext *, JSValue *, int, JSValue *);
void js_wifi_monitor_session_finalizer(JSContext *, void *);
JSValue js_wifi_monitor_session_get_queue(JSContext *, JSValue *, int, JSValue *);
JSValue js_wifi_monitor_session_configure(JSContext *, JSValue *, int, JSValue *);
JSValue js_wifi_monitor_session_status(JSContext *, JSValue *, int, JSValue *);
JSValue js_wifi_monitor_session_stats(JSContext *, JSValue *, int, JSValue *);
JSValue js_wifi_monitor_session_receive(JSContext *, JSValue *, int, JSValue *);
JSValue js_wifi_monitor_session_start(JSContext *, JSValue *, int, JSValue *);
JSValue js_wifi_monitor_session_stop(JSContext *, JSValue *, int, JSValue *);
JSValue js_wifi_monitor_session_close(JSContext *, JSValue *, int, JSValue *);
JSValue js_wifi_monitor_frame_constructor(JSContext *, JSValue *, int, JSValue *);
void js_wifi_monitor_frame_finalizer(JSContext *, void *);
JSValue js_wifi_monitor_frame_bytes(JSContext *, JSValue *, int, JSValue *);
JSValue js_wifi_monitor_frame_copy_bytes(JSContext *, JSValue *, int, JSValue *);
JSValue js_wifi_monitor_frame_source(JSContext *, JSValue *, int, JSValue *);
JSValue js_wifi_monitor_frame_close(JSContext *, JSValue *, int, JSValue *);
JSValue js_wifi_monitor_session_receive_batch(JSContext *, JSValue *, int, JSValue *);
JSValue js_wifi_monitor_batch_constructor(JSContext *, JSValue *, int, JSValue *);
void js_wifi_monitor_batch_finalizer(JSContext *, void *);
JSValue js_wifi_monitor_batch_info(JSContext *, JSValue *, int, JSValue *);
JSValue js_wifi_monitor_batch_bytes(JSContext *, JSValue *, int, JSValue *);
JSValue js_wifi_monitor_batch_source(JSContext *, JSValue *, int, JSValue *);
JSValue js_wifi_monitor_batch_close(JSContext *, JSValue *, int, JSValue *);
#endif
