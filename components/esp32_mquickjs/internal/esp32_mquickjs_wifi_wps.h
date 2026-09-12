#pragma once
#include "esp32_mquickjs_types.h"
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI && CONFIG_ESP_NETIF_USES_TCPIP_WITH_BSD_API && CONFIG_LWIP_IPV4
bool esp32_mquickjs_init_wifi_wps_runtime(JSContext *, esp32_mquickjs_runtime_t *);
JSValue js_wifi_wps_capabilities(JSContext *, JSValue *, int, JSValue *);
JSValue js_wifi_wps_global_status(JSContext *, JSValue *, int, JSValue *);
JSValue js_wifi_wps_start(JSContext *, JSValue *, int, JSValue *);
JSValue js_wifi_wps_constructor(JSContext *, JSValue *, int, JSValue *);
void js_wifi_wps_finalizer(JSContext *, void *);
JSValue js_wifi_wps_status(JSContext *, JSValue *, int, JSValue *);
JSValue js_wifi_wps_watch(JSContext *, JSValue *, int, JSValue *);
JSValue js_wifi_wps_receive(JSContext *, JSValue *, int, JSValue *);
JSValue js_wifi_wps_close(JSContext *, JSValue *, int, JSValue *);
JSValue js_wifi_wps_cancel(JSContext *, JSValue *, int, JSValue *);
bool esp32_mquickjs_wifi_wps_poll_observations(bool close);
#endif
