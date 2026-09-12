#pragma once
#include "esp32_mquickjs_wifi_twt_information_timer.h"
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI && CONFIG_SOC_WIFI_HE_SUPPORT && CONFIG_IDF_TARGET_ESP32C5
#include "esp_wifi_he_types.h"
#include <stddef.h>
/* The pinned producer multiplies milliseconds by 1000 in a 32-bit register. */
#define ESP32_MQUICKJS_WIFI_TWT_SUSPEND_MAX_MS (UINT32_MAX / 1000U)
typedef struct {
    uint32_t identity, setup_identity, tx_identity, timer_identity, duration_ms;
    esp_err_t submit_error, native_error, observation_error, cleanup_error;
    uint8_t flow;
    bool submitting, complete, event_seen, abandoned, resume, tx_complete, resume_complete;
} esp32_mquickjs_wifi_twt_information_result_t;
/* One boot-stable information lane. Reserve in the native task after exact
 * Agreement admission, before sending. IDs never wrap or repeat. A public
 * timeout only abandons the result; TX/timer storage has its own lifetime. */
esp_err_t esp32_mquickjs_wifi_twt_information_begin_native(uint32_t setup_identity,
    const esp32_mquickjs_wifi_twt_information_identity_t *native, uint32_t duration_ms, bool resume, uint32_t *identity);
void esp32_mquickjs_wifi_twt_information_submitted_native(uint32_t identity, esp_err_t error);
bool esp32_mquickjs_wifi_twt_information_read(uint32_t identity, esp32_mquickjs_wifi_twt_information_result_t *out);
bool esp32_mquickjs_wifi_twt_information_pending(void);
bool esp32_mquickjs_wifi_twt_information_snapshot(esp32_mquickjs_wifi_twt_information_result_t *out);
void esp32_mquickjs_wifi_twt_information_abandon(uint32_t identity);
/* Native task only. setup_identity != 0 abandons that Agreement's operation
 * before teardown. Zero only reaps operations already abandoned by callers.
 * A live EB or publishing callback retains the lane; no timer is cancelled. */
esp_err_t esp32_mquickjs_wifi_twt_information_reap_native(uint32_t setup_identity);
bool esp32_mquickjs_wifi_twt_information_producer_allowed(void *node, uint32_t flow,
    uint32_t size, uint32_t all, uint32_t duration_ms);
void esp32_mquickjs_wifi_twt_information_bind_native(
    const esp32_mquickjs_wifi_twt_information_identity_t *native, uint32_t tx_identity);
bool esp32_mquickjs_wifi_twt_information_callback_begin(uint32_t tx_identity);
void esp32_mquickjs_wifi_twt_information_callback_end(uint32_t tx_identity, bool valid);
/* Numeric identity of the actual replacement timer, captured in the exact
 * information TX callback. Old queued timers cannot complete a later resume. */
void esp32_mquickjs_wifi_twt_information_timer_bound_native(
    const esp32_mquickjs_wifi_twt_information_identity_t *native, uint32_t timer_identity);
void esp32_mquickjs_wifi_twt_information_timer_finished_native(
    const esp32_mquickjs_wifi_twt_information_identity_t *native, uint32_t timer_identity, esp_err_t error);
/* Native queue maintenance reports association/timer failure while a resume
 * waits. It does not send a second frame, cancel timers or release the lane. */
void esp32_mquickjs_wifi_twt_information_refresh_native(void);
/* During the exact producer/callback scope, copy the native observation and
 * publish only after native side effects return. Unmanaged events are copied
 * and posted immediately. Observer saturation never blocks completion. */
esp_err_t esp32_mquickjs_wifi_twt_information_post(const void *data, size_t size);
/* Worker-facing native queue adapters; no SDK calls on the runtime thread. */
esp_err_t esp32_mquickjs_wifi_twt_sdk_information_submit(uint32_t setup_identity, uint32_t duration_ms, bool resume, uint32_t *identity);
esp_err_t esp32_mquickjs_wifi_twt_sdk_information_submit_native(uint32_t setup_identity, uint32_t duration_ms, bool resume, uint32_t *identity);
esp_err_t esp32_mquickjs_wifi_twt_sdk_information_reap(uint32_t setup_identity);
/* The reviewed native resume helper would enable PS when it is currently
 * NONE. Preserve the existing policy by rejecting that mutation beforehand. */
bool esp32_mquickjs_wifi_twt_sdk_information_resume_allowed_native(
    const esp32_mquickjs_wifi_twt_information_identity_t *native);
bool esp32_mquickjs_wifi_twt_sdk_information_resumed_native(
    const esp32_mquickjs_wifi_twt_information_identity_t *native);
#endif
