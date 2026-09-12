#pragma once
#include "sdkconfig.h"
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI && CONFIG_SOC_WIFI_HE_SUPPORT && CONFIG_IDF_TARGET_ESP32C5
#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>
typedef enum {
    ESP32_MQUICKJS_WIFI_TWT_PROBE_TIMER_NONE = 0,
    ESP32_MQUICKJS_WIFI_TWT_PROBE_TIMER_ARGUMENT,
    ESP32_MQUICKJS_WIFI_TWT_PROBE_TIMER_IDENTITY,
    ESP32_MQUICKJS_WIFI_TWT_PROBE_TIMER_CREATE,
    ESP32_MQUICKJS_WIFI_TWT_PROBE_TIMER_START,
    ESP32_MQUICKJS_WIFI_TWT_PROBE_TIMER_STOP,
    ESP32_MQUICKJS_WIFI_TWT_PROBE_TIMER_DELETE,
    ESP32_MQUICKJS_WIFI_TWT_PROBE_TIMER_POST,
} esp32_mquickjs_wifi_twt_probe_timer_stage_t;
typedef struct {
    uint32_t last_identity, current_identity, failed_identity;
    esp_err_t fault;
    int post_error;
    esp_err_t cleanup_error;
    esp32_mquickjs_wifi_twt_probe_timer_stage_t fault_stage;
    uint8_t phase;
    bool fired, cleanup_pending;
} esp32_mquickjs_wifi_twt_probe_timer_snapshot_t;
/* Boot-scoped values only. An ID is never reused, including after driver or
 * runtime restart. Invalidation discards authority, not an SDK drain proof.
 * No node pointer/handle escapes. The existing SDK timer allocation is owned
 * explicitly; a failed delete retains its handle for suffix-only retry. */
void esp32_mquickjs_wifi_twt_probe_timer_snapshot(esp32_mquickjs_wifi_twt_probe_timer_snapshot_t *out);
/* Native submit/TX callback return boundary only. Releases only the exact
 * current probe's active/PM reference after a local timer failure, retains
 * failed timer cleanup, and records the original error without inventing RF
 * timeout/success. Neither this nor timer deletion proves TX/event drain. */
esp_err_t esp32_mquickjs_wifi_twt_probe_timer_finish_native(void);
/* Only after exact result identity admission, in the native ioctl task.
 * Invalidates numeric callbacks, deletes the owned timer, and releases the
 * exact active node/PM reference. A failed delete retains its handle; retry
 * performs only remaining cleanup. Never waits for esp_timer callback exit. */
esp_err_t esp32_mquickjs_wifi_twt_probe_timer_cancel_native(void);
#endif
