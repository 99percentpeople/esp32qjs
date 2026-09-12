#pragma once
#include "sdkconfig.h"
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI && CONFIG_SOC_WIFI_HE_SUPPORT && CONFIG_IDF_TARGET_ESP32C5
#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>
typedef struct {
    uintptr_t node;
    int16_t request_ids[8];
    uint8_t flows, control;
} esp32_mquickjs_wifi_twt_information_identity_t;
typedef enum {
    ESP32_MQUICKJS_WIFI_TWT_INFORMATION_ARGUMENT = 1,
    ESP32_MQUICKJS_WIFI_TWT_INFORMATION_ALLOCATE,
    ESP32_MQUICKJS_WIFI_TWT_INFORMATION_CREATE,
    ESP32_MQUICKJS_WIFI_TWT_INFORMATION_START,
    ESP32_MQUICKJS_WIFI_TWT_INFORMATION_POST,
    ESP32_MQUICKJS_WIFI_TWT_INFORMATION_STOP,
    ESP32_MQUICKJS_WIFI_TWT_INFORMATION_DELETE,
    ESP32_MQUICKJS_WIFI_TWT_INFORMATION_NATIVE,
} esp32_mquickjs_wifi_twt_information_stage_t;
typedef struct {
    uint32_t last_identity, revision, reserved_bytes;
    esp_err_t fault;
    uint8_t fault_stage, fault_slot, active_mask, payload_mask, busy_mask, cleanup_mask;
} esp32_mquickjs_wifi_twt_information_timer_snapshot_t;
/* Exact reviewed SDK information timer table only. setfn consumes the native
 * nine-byte heap argument for the reviewed callback, including on failure.
 * Callback/native queues carry a number. The original native process receives
 * the heap argument only after identity validation and then owns its free. */
bool esp32_mquickjs_wifi_twt_information_timer_setfn(void *timer, void *callback, void *argument);
bool esp32_mquickjs_wifi_twt_information_timer_disarm(void *timer);
bool esp32_mquickjs_wifi_twt_information_timer_done(void *timer);
bool esp32_mquickjs_wifi_twt_information_timer_arm(void *timer, uint64_t us, bool repeat);
/* Native whole-connection close only. Revokes all-flow as well as single-flow
 * numeric callbacks; retained delete failures still require normal retirement. */
void esp32_mquickjs_wifi_twt_information_timers_connection_closed_native(void);
void esp32_mquickjs_wifi_twt_information_timer_snapshot(esp32_mquickjs_wifi_twt_information_timer_snapshot_t *out);
/* Native task only. Absence plus revision, not external queue ordering. */
esp_err_t esp32_mquickjs_wifi_twt_information_timer_quiescent_native(int16_t request_id, uint32_t *revision);
/* Read-only admission for replacing a single-flow timer after successful TX.
 * Never cancels an old timer before the replacement frame completes. */
esp_err_t esp32_mquickjs_wifi_twt_information_timer_replaceable_native(int16_t request_id, unsigned flow);
/* After native flow retirement: an included retained flow must have exact
 * managed close consent before its shared timer may be cancelled. Unknown or
 * nonclosing requests block the whole preflight. No driver/table mutation. */
esp_err_t esp32_mquickjs_wifi_twt_information_timer_cleanup_native(int16_t request_id, uint8_t retired_flows);
bool esp32_mquickjs_wifi_twt_sdk_information_capture_native(unsigned slot, const void *argument,
    esp32_mquickjs_wifi_twt_information_identity_t *out);
bool esp32_mquickjs_wifi_twt_sdk_information_matches_native(unsigned slot,
    const esp32_mquickjs_wifi_twt_information_identity_t *identity);
#endif
