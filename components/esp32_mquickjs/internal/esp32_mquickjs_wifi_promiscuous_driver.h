#pragma once
#include "sdkconfig.h"
#if CONFIG_ESP32_MQUICKJS_WIFI_RADIO
#include "esp_wifi.h"
#include "esp32_mquickjs_wifi_promiscuous_broker.h"

typedef struct {
    bool enabled, receive, preserve_baseline;
    uint32_t packet_mask, control_mask;
} esp32_mquickjs_wifi_promiscuous_demand_t;
typedef struct {
    bool claimed, cleanup_pending;
    const char *error_stage, *cleanup_stage;
    esp_err_t error, cleanup_error;
} esp32_mquickjs_wifi_promiscuous_driver_status_t;

/* Translate common requirements to pinned public SDK masks. The hardware
 * filter can be a superset; each subscriber's native filter is authoritative. */
esp_err_t esp32_mquickjs_wifi_promiscuous_driver_demand(
    const esp32_mquickjs_wifi_promiscuous_snapshot_t *requirements,
    bool enabled, bool preserve_baseline, esp32_mquickjs_wifi_promiscuous_demand_t *output);

/* Radio mutation mutex is required for every call. Only this component writes
 * promiscuous callback/enable/filter settings. Driver init is framework-owned;
 * the initial RX callback is known NULL, not recovered using a nonexistent getter.
 * receive=false preserves the pre-claim filter configuration (CSI enable only).
 * enabled=false restores the complete pre-claim state. Failed partial mutations
 * roll back; failed rollback retains ownership until recover succeeds. */
esp_err_t esp32_mquickjs_wifi_promiscuous_driver_apply(
    const esp32_mquickjs_wifi_promiscuous_demand_t *demand);
esp_err_t esp32_mquickjs_wifi_promiscuous_driver_recover(void);
void esp32_mquickjs_wifi_promiscuous_driver_status(
    esp32_mquickjs_wifi_promiscuous_driver_status_t *output);
#endif
