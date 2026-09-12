#pragma once
#include "esp32_mquickjs_types.h"
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI && CONFIG_ESP_WIFI_WPS_SOFTAP_REGISTRAR && CONFIG_ESP_NETIF_USES_TCPIP_WITH_BSD_API && CONFIG_LWIP_IPV4
#include "esp_wps.h"
bool esp32_mquickjs_wifi_wps_ap_capture_options(JSContext *, JSGCRef *, esp_wps_config_t *, uint32_t *);
bool esp32_mquickjs_init_wifi_wps_ap_runtime(JSContext *, esp32_mquickjs_runtime_t *);
JSValue js_wifi_wps_ap_global_status(JSContext *, JSValue *, int, JSValue *);
JSValue js_wifi_wps_ap_start(JSContext *, JSValue *, int, JSValue *);
JSValue js_wifi_wps_ap_constructor(JSContext *, JSValue *, int, JSValue *);
void js_wifi_wps_ap_finalizer(JSContext *, void *);
JSValue js_wifi_wps_ap_status(JSContext *, JSValue *, int, JSValue *);
JSValue js_wifi_wps_ap_watch(JSContext *, JSValue *, int, JSValue *);
JSValue js_wifi_wps_ap_receive(JSContext *, JSValue *, int, JSValue *);
JSValue js_wifi_wps_ap_close(JSContext *, JSValue *, int, JSValue *);
JSValue js_wifi_wps_ap_cancel(JSContext *, JSValue *, int, JSValue *);
bool esp32_mquickjs_wifi_wps_ap_poll_observations(bool close);
#endif
