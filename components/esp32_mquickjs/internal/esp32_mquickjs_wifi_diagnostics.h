#pragma once
#include "esp32_mquickjs_types.h"
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI
JSValue js_wifi_diagnostics_snapshot(JSContext *, JSValue *, int, JSValue *);
JSValue js_wifi_diagnostics_dump_driver_stats(JSContext *, JSValue *, int, JSValue *);
JSValue js_wifi_diagnostics_idf_api_coverage(JSContext *, JSValue *, int, JSValue *);
JSValue js_wifi_diagnostics_reset_counters(JSContext *, JSValue *, int, JSValue *);
#endif
