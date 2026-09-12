#pragma once
#include "esp32_mquickjs_types.h"
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI && CONFIG_ESP_NETIF_USES_TCPIP_WITH_BSD_API && CONFIG_LWIP_IPV4
bool esp32_mquickjs_init_wifi_smartconfig_runtime(JSContext *, esp32_mquickjs_runtime_t *);
JSValue js_wifi_smartconfig_capabilities(JSContext *, JSValue *, int, JSValue *);
JSValue js_wifi_smartconfig_global_status(JSContext *, JSValue *, int, JSValue *);
JSValue js_wifi_smartconfig_start(JSContext *, JSValue *, int, JSValue *);
JSValue js_wifi_smartconfig_constructor(JSContext *, JSValue *, int, JSValue *);
void js_wifi_smartconfig_finalizer(JSContext *, void *);
JSValue js_wifi_smartconfig_status(JSContext *, JSValue *, int, JSValue *);
JSValue js_wifi_smartconfig_watch(JSContext *, JSValue *, int, JSValue *);
JSValue js_wifi_smartconfig_receive(JSContext *, JSValue *, int, JSValue *);
JSValue js_wifi_smartconfig_close(JSContext *, JSValue *, int, JSValue *);
#endif
