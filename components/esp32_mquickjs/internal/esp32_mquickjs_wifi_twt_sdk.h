#pragma once
#include "sdkconfig.h"
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI && CONFIG_SOC_WIFI_HE_SUPPORT
#include "esp_err.h"
#include "esp_wifi_he_types.h"
#include <stdbool.h>
#include <stdint.h>
#include "esp32_mquickjs_wifi_twt_tx.h"
#include "esp32_mquickjs_wifi_twt_probe_timer.h"
#include "esp32_mquickjs_wifi_twt_probe_result.h"
#include "esp32_mquickjs_wifi_twt_probe_wake.h"
#include "esp32_mquickjs_wifi_twt_setup_timer.h"
#include "esp32_mquickjs_wifi_twt_setup_result.h"
#include "esp32_mquickjs_wifi_twt_teardown_tx.h"
#include "esp32_mquickjs_wifi_twt_information_timer.h"
#include "esp32_mquickjs_wifi_twt_broadcast_timer.h"

/* Internal, bounded value snapshot of the reviewed native C5 tables. No native
 * pointer or borrowed timer handle escapes. Timer bits mean handle PRESENT,
 * not armed; absent handles do not prove callbacks have finished. */
typedef struct {
    int16_t individual_ids[8], individual_temporary_ids[8], individual_pending_ids[8];
    uint8_t individual_pending_flows[8];
    uint32_t broadcast_id_bitmap, broadcast_setup_timer_mask;
    uint8_t individual_flow_bitmap, individual_pending_mask, individual_setup_timer_mask;
    uint8_t individual_information_timer_mask, individual_suspend_bitmap, individual_resume_bitmap;
    bool probe_timer_present, probe_active;
    uint8_t probe_phase; /* UINT8_MAX when there is no active probe. */
#if CONFIG_IDF_TARGET_ESP32C5
    esp32_mquickjs_wifi_twt_tx_snapshot_t tx;
    esp32_mquickjs_wifi_twt_probe_timer_snapshot_t probe_timer;
    esp32_mquickjs_wifi_twt_probe_result_snapshot_t probe_result;
    esp32_mquickjs_wifi_twt_probe_wake_snapshot_t probe_wake;
    esp32_mquickjs_wifi_twt_setup_timer_snapshot_t setup_timer;
    esp32_mquickjs_wifi_twt_setup_results_snapshot_t setup_results;
    esp32_mquickjs_wifi_twt_teardown_tx_snapshot_t teardown_tx;
    esp32_mquickjs_wifi_twt_information_timer_snapshot_t information_timer;
    esp32_mquickjs_wifi_btwt_timer_snapshot_t broadcast_timer;
#endif
} esp32_mquickjs_wifi_twt_sdk_snapshot_t;

/* Executes in the existing reviewed native ioctl queue. No RF/PS mutation or
 * implicit start. Output is committed only on success. Other HE targets remain
 * unsupported until their archive layout is reviewed. Never use an all-zero
 * snapshot alone to attest SDK or event-loop retirement. */
esp_err_t esp32_mquickjs_wifi_twt_sdk_snapshot(esp32_mquickjs_wifi_twt_sdk_snapshot_t *output);
/* Only the native ioctl adapter may call this, in the Wi-Fi task. */
bool esp32_mquickjs_wifi_twt_sdk_broadcast_node_matches_native(uintptr_t node);
/* Value-only adapter; the original SDK consumes a normalized synchronous view,
 * never the live driver EB and never bytes beyond the broadcast IE. */
void esp32_mquickjs_wifi_twt_sdk_broadcast_tx_complete_native(uintptr_t node,
    uint8_t dialog, const uint8_t parameter[17], uint8_t status);
esp_err_t esp32_mquickjs_wifi_twt_sdk_snapshot_native(esp32_mquickjs_wifi_twt_sdk_snapshot_t *output);
/* Cancel the latest accepted probe by exact identity on the native queue.
 * Does not stop TWT agreements, disconnect Station, or recycle an in-flight
 * TX. ESP_OK is cancellation acknowledgement, not a resource drain proof.
 * A failed cleanup can be retried with the same identity. */
esp_err_t esp32_mquickjs_wifi_twt_sdk_probe_cancel(uint32_t identity);
#if CONFIG_IDF_TARGET_ESP32C5
/* A copied discovery result, not Agreement ownership. The native getter emits
 * at most 32 ten-byte records and marks each filled record in_use. Always use
 * the full capacity; separate count/get calls outside the native task race AP
 * advertisement updates. No SDK/node pointer escapes this structure. */
#define ESP32_MQUICKJS_WIFI_TWT_MAX_BROADCAST 32U
typedef struct {
    esp_wifi_btwt_info_t schedules[ESP32_MQUICKJS_WIFI_TWT_MAX_BROADCAST];
    uint32_t joined_bitmap;
    uint8_t count;
} esp32_mquickjs_wifi_twt_broadcast_snapshot_t;
esp_err_t esp32_mquickjs_wifi_twt_sdk_broadcast_snapshot(esp32_mquickjs_wifi_twt_broadcast_snapshot_t *);
esp_err_t esp32_mquickjs_wifi_twt_sdk_broadcast_snapshot_native(esp32_mquickjs_wifi_twt_broadcast_snapshot_t *);
/* Broadcast pending cancellation and value-only quiescence use the private
 * native queue. Errors leave output unchanged. No owner release or RF undo. */
esp_err_t esp32_mquickjs_wifi_twt_sdk_broadcast_teardown(unsigned slot, uint32_t identity);
esp_err_t esp32_mquickjs_wifi_twt_sdk_broadcast_teardown_native(unsigned slot, uint32_t identity);
void esp32_mquickjs_wifi_twt_sdk_broadcast_teardown_complete_native(uintptr_t node, uint8_t slot, uint8_t status);
esp_err_t esp32_mquickjs_wifi_twt_sdk_broadcast_cancel(unsigned slot, uint32_t identity);
esp_err_t esp32_mquickjs_wifi_twt_sdk_broadcast_quiescent(unsigned slot, uint32_t identity, esp32_mquickjs_wifi_btwt_cut_t *out);
bool esp32_mquickjs_wifi_twt_sdk_broadcast_established_native(unsigned slot);
esp_err_t esp32_mquickjs_wifi_twt_sdk_broadcast_release(unsigned slot, uint32_t identity,
    const esp32_mquickjs_wifi_btwt_cut_t *cut, uint32_t sequence);
/* Cancel only the exact still-pending individual setup. Existing Agreement
 * returns NOT_FINISHED (needs teardown); ambiguous native flow ownership is
 * INVALID_STATE. Does not disconnect, alter shared PS, release the result, or
 * prove TX/timer-task/event/RF retirement. Submitting records cannot cancel. */
esp_err_t esp32_mquickjs_wifi_twt_sdk_setup_cancel(uint32_t identity);
esp_err_t esp32_mquickjs_wifi_twt_sdk_setup_cancel_native(uint32_t identity);
/* Internal single-flow teardown submission. Exact setup result/request/flow
 * must still own the native established slot. One attempt, raw submit error;
 * neither success nor an event proves completion or permits retry. The Radio
 * Agreement owner keeps its lease through TX/PM and joint retirement. */
esp_err_t esp32_mquickjs_wifi_twt_sdk_setup_teardown(uint32_t identity, uint8_t flow);
esp_err_t esp32_mquickjs_wifi_twt_sdk_setup_teardown_native(uint32_t identity, uint8_t flow);
esp_err_t esp32_mquickjs_wifi_twt_sdk_teardown_tx_quiescent(uint32_t identity, uint32_t *revision);
esp_err_t esp32_mquickjs_wifi_twt_sdk_teardown_tx_release(uint32_t identity, uint32_t revision);
typedef struct { uint32_t tx_revision, timer_revision, information_revision, teardown_revision; } esp32_mquickjs_wifi_twt_setup_cut_t;
/* After exact pending cancellation or successful single-flow teardown and
 * cleanup. These value checks do not supply timer/native/event ordering.
 * release removes the result only; its caller must retain owner storage and
 * release a nonzero teardown_revision TX scope before releasing Radio. */
esp_err_t esp32_mquickjs_wifi_twt_sdk_setup_quiescent(uint32_t identity, esp32_mquickjs_wifi_twt_setup_cut_t *cut);
esp_err_t esp32_mquickjs_wifi_twt_sdk_setup_quiescent_native(uint32_t identity, esp32_mquickjs_wifi_twt_setup_cut_t *cut);
esp_err_t esp32_mquickjs_wifi_twt_sdk_setup_release(uint32_t identity,
    const esp32_mquickjs_wifi_twt_setup_cut_t *cut, uint32_t sequence);
esp_err_t esp32_mquickjs_wifi_twt_sdk_setup_release_native(uint32_t identity,
    const esp32_mquickjs_wifi_twt_setup_cut_t *cut, uint32_t sequence);
typedef struct { uint32_t tx_revision, timer_identity; } esp32_mquickjs_wifi_twt_probe_cut_t;
/* A nonzero output identity is owned even on submission error. Retirement is
 * mandatory before another managed OR ordinary SDK probe can be submitted.
 * Caller supplies Radio admission; no implicit driver start/configuration. */
esp_err_t esp32_mquickjs_wifi_twt_sdk_probe_submit(uint32_t timeout_ms, uint32_t *identity);
/* Native state and TX pins must be absent; this is not timer/event ordering. */
esp_err_t esp32_mquickjs_wifi_twt_sdk_probe_quiescent(uint32_t identity, esp32_mquickjs_wifi_twt_probe_cut_t *cut);
/* After external timer/native ordering and the exact event marker. The native
 * queue rechecks the same cut before relinquishing the pin. */
esp_err_t esp32_mquickjs_wifi_twt_sdk_probe_release(uint32_t identity,
    const esp32_mquickjs_wifi_twt_probe_cut_t *cut, uint32_t sequence);
esp_err_t esp32_mquickjs_wifi_twt_sdk_probe_submit_native(uint32_t timeout_ms, uint32_t *identity);
esp_err_t esp32_mquickjs_wifi_twt_sdk_probe_quiescent_native(uint32_t identity, esp32_mquickjs_wifi_twt_probe_cut_t *cut);
esp_err_t esp32_mquickjs_wifi_twt_sdk_probe_release_native(uint32_t identity,
    const esp32_mquickjs_wifi_twt_probe_cut_t *cut, uint32_t sequence);
/* Same native ioctl task only: association and absence of an earlier probe's
 * pending state/timer, not a timer callback or event retirement proof. */
esp_err_t esp32_mquickjs_wifi_twt_sdk_probe_admit_native(void);
bool esp32_mquickjs_wifi_twt_sdk_probe_capture_native(const void *argument, uintptr_t *node, uint8_t *phase);
bool esp32_mquickjs_wifi_twt_sdk_probe_matches_native(uintptr_t node, uint8_t phase);
bool esp32_mquickjs_wifi_twt_sdk_probe_abort_native(uintptr_t node, uint8_t phase);
bool esp32_mquickjs_wifi_twt_sdk_probe_active_native(void);
esp_err_t esp32_mquickjs_wifi_twt_sdk_probe_cancel_native(uint32_t identity);
#endif
#endif
