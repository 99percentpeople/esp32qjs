#pragma once
#include "sdkconfig.h"
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI && CONFIG_SOC_WIFI_HE_SUPPORT && CONFIG_IDF_TARGET_ESP32C5
#include "esp_err.h"
#include "esp_wifi_he_types.h"
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint32_t identity, observation_calls, fence_sequence;
    esp_err_t submit_error, observation_error, control_error, fault, cancel_error;
    wifi_event_sta_itwt_probe_t event;
    bool submitting, submitted, event_seen, cancel_requested, cancel_complete;
    bool owned, fence_pending, event_fenced;
} esp32_mquickjs_wifi_twt_probe_result_snapshot_t;

/* Latest accepted native probe, copied before the default event queue. The
 * boot identity never wraps or resets. Managed submission additionally pins
 * this record until joint retirement. This is not a Radio lease; an owner
 * must still match identity when consuming it. A successful event means AP Beacon/Probe Response liveness,
 * not correlation with a particular transmitted request or a TSF readback.
 * Observation failure does not change the native operation result. */
void esp32_mquickjs_wifi_twt_probe_result_snapshot(esp32_mquickjs_wifi_twt_probe_result_snapshot_t *out);
/* Only the native submission wrapper calls these, after association, previous
 * probe timer/pending state, and exact TX ledger admission. The wrapper keeps
 * its scope through any deferred TX callback. No caller-supplied identity. */
esp_err_t esp32_mquickjs_wifi_twt_probe_result_begin_native(uint32_t *identity);
void esp32_mquickjs_wifi_twt_probe_result_submitted_native(uint32_t identity, esp_err_t error);
void esp32_mquickjs_wifi_twt_probe_result_failed_native(esp_err_t error);
/* Native ioctl task only. Exact identity is checked before any cancellation
 * mutation. A prior event remains the winner; observations after cancellation
 * starts cannot replace it. cancel_complete acknowledges only native probe
 * cancellation, NOT TX, timer callback or default-event-loop retirement.
 * cancel_error is the latest cleanup attempt; original timer faults remain. */
esp_err_t esp32_mquickjs_wifi_twt_probe_result_cancel_begin_native(uint32_t identity);
void esp32_mquickjs_wifi_twt_probe_result_cancel_end_native(uint32_t identity, esp_err_t error);
/* Managed submission pins this native record before leaving the ioctl task.
 * Even callers of the ordinary SDK probe API cannot replace a pinned record. */
bool esp32_mquickjs_wifi_twt_probe_result_claim_native(uint32_t identity);
bool esp32_mquickjs_wifi_twt_probe_result_release_native(uint32_t identity, uint32_t sequence);
typedef struct { uint32_t identity, sequence; } esp32_mquickjs_wifi_twt_probe_event_fence_t;
#define ESP32_MQUICKJS_WIFI_TWT_PROBE_FENCE_EVENT 4
/* After timer/native ordering and native quiescence only. Boot-owned event
 * handler consumes copied numbers, never an owner/runtime address. */
esp_err_t esp32_mquickjs_wifi_twt_probe_result_post_fence(uint32_t identity, uint32_t *sequence);
void esp32_mquickjs_wifi_twt_probe_result_observe_fence(const esp32_mquickjs_wifi_twt_probe_event_fence_t *event);
#endif
