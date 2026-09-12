#pragma once
#include "sdkconfig.h"
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI && CONFIG_SOC_WIFI_HE_SUPPORT && CONFIG_IDF_TARGET_ESP32C5
#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>
/* Only the eight C5 individual setup timers, including their response/dwell
 * phases. Broadcast and information/suspend timers remain separate work. */
typedef enum {
    ESP32_MQUICKJS_WIFI_TWT_SETUP_TIMER_ARGUMENT = 1,
    ESP32_MQUICKJS_WIFI_TWT_SETUP_TIMER_IDENTITY,
    ESP32_MQUICKJS_WIFI_TWT_SETUP_TIMER_ALLOCATE,
    ESP32_MQUICKJS_WIFI_TWT_SETUP_TIMER_CREATE,
    ESP32_MQUICKJS_WIFI_TWT_SETUP_TIMER_START,
    ESP32_MQUICKJS_WIFI_TWT_SETUP_TIMER_STOP,
    ESP32_MQUICKJS_WIFI_TWT_SETUP_TIMER_DELETE,
    ESP32_MQUICKJS_WIFI_TWT_SETUP_TIMER_POST,
    ESP32_MQUICKJS_WIFI_TWT_SETUP_TIMER_NATIVE,
} esp32_mquickjs_wifi_twt_setup_timer_stage_t;
typedef struct {
    uintptr_t node; /* address VALUE only; never dereference an old node */
    int16_t request_id;
    uint8_t dialog, flow;
} esp32_mquickjs_wifi_twt_setup_timer_identity_t;
typedef struct {
    uint32_t last_identity, revision, reserved_bytes;
    esp_err_t fault;
    uint8_t active_mask, fired_mask, fault_mask, cleanup_mask;
    uint8_t fault_slot, fault_stage;
} esp32_mquickjs_wifi_twt_setup_timer_snapshot_t;
/* Return true for an owned timer address even on error, so the caller cannot
 * fall back to the legacy abort/borrowed-argument implementation. SDK timer
 * operations run in the native Wi-Fi task; callbacks carry numbers only. */
bool esp32_mquickjs_wifi_twt_setup_timer_setfn(void *timer, void *callback, void *argument);
bool esp32_mquickjs_wifi_twt_setup_timer_disarm(void *timer);
bool esp32_mquickjs_wifi_twt_setup_timer_done(void *timer);
bool esp32_mquickjs_wifi_twt_setup_timer_arm(void *timer, uint64_t us, bool repeat);
esp_err_t esp32_mquickjs_wifi_twt_setup_timer_error(void);
void esp32_mquickjs_wifi_twt_setup_timer_snapshot(esp32_mquickjs_wifi_twt_setup_timer_snapshot_t *out);
/* Only the native SDK adapter reads the current associated node/pending table.
 * phase is ioctl operation 26 (response) or 27 (dwell). */
bool esp32_mquickjs_wifi_twt_sdk_setup_timer_capture_native(unsigned slot, uint8_t phase,
    const void *argument, esp32_mquickjs_wifi_twt_setup_timer_identity_t *out);
bool esp32_mquickjs_wifi_twt_sdk_setup_timer_matches_native(unsigned slot, uint8_t phase,
    const esp32_mquickjs_wifi_twt_setup_timer_identity_t *identity);
/* Native Wi-Fi task only. Bind a setup TX to the response timer's non-reused
 * identity before output; completion must still match phase zero and the
 * temporary request ID. These checks neither consume nor retire the timer. */
bool esp32_mquickjs_wifi_twt_setup_tx_capture_native(uintptr_t node, uint8_t dialog,
    uint8_t flow, uint32_t *identity);
bool esp32_mquickjs_wifi_twt_setup_tx_matches_native(uint32_t identity, uint8_t dialog, uint8_t flow);
bool esp32_mquickjs_wifi_twt_sdk_setup_tx_matches_native(unsigned slot,
    const esp32_mquickjs_wifi_twt_setup_timer_identity_t *identity);
/* Native task only, after exact result/request admission. Includes orphaned
 * retained handles for this request. Preflight every slot before mutation;
 * pending_mask comes from the current native pending table. No TX/RF/event
 * retirement claim. Stop/delete failures keep the handle and retry suffix. */
/* Native whole-connection close: revoke all numeric timer/TX authorities first,
 * then attempt each known timer cleanup. Failed handles remain for owner retry. */
void esp32_mquickjs_wifi_twt_setup_timers_connection_closed_native(void);
esp_err_t esp32_mquickjs_wifi_twt_setup_timer_cancel_native(int16_t request_id, uint8_t pending_mask);
/* Native queue only: no matching active authority or retained handle. This
 * does not establish timer-task/callback ordering; caller supplies a marker. */
esp_err_t esp32_mquickjs_wifi_twt_setup_timer_quiescent_native(int16_t request_id, uint32_t *revision);
#endif
