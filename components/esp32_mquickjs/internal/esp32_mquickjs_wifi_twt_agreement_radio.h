#pragma once
#include "esp32_mquickjs_wifi_twt_setup_submit.h"
#include "esp32_mquickjs_wifi_twt_setup_retire.h"
#include "esp32_mquickjs_wifi_twt_information.h"
#include "esp32_mquickjs_wifi_twt_broadcast_submit.h"
#include "esp32_mquickjs_wifi_twt_broadcast_retire.h"
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI && CONFIG_SOC_WIFI_HE_SUPPORT && CONFIG_IDF_TARGET_ESP32C5
#define ESP32_MQUICKJS_WIFI_TWT_MAX_INDIVIDUAL 8U
typedef struct {
    esp32_mquickjs_wifi_twt_token_t token;
    wifi_itwt_setup_config_t requested;
    esp32_mquickjs_wifi_twt_setup_dispatch_t dispatch;
    esp32_mquickjs_wifi_twt_setup_result_t native;
    esp_err_t submit_error, cleanup_error;
    const char *cleanup_stage;
    bool dispatching, closing, result_released, native_retired;
} esp32_mquickjs_wifi_twt_individual_radio_state_t;
/* Worker only. Already started/associated Station; no implicit mode, channel,
 * PS or connection mutation. Eight boot-stable owners share the Radio lease
 * registry. Nonzero token on error still requires close. Request IDs and token
 * identities never repeat, including failed submissions. */
esp_err_t esp32_mquickjs_wifi_radio_twt_individual_submit(
    const esp32_mquickjs_wifi_itwt_options_t *options, esp32_mquickjs_wifi_twt_token_t *token);
/* Nonblocking native value snapshot. Output unchanged if identity no longer
 * matches or a native result was retired during the copy. */
bool esp32_mquickjs_wifi_radio_twt_individual_status(const esp32_mquickjs_wifi_twt_token_t *token,
    esp32_mquickjs_wifi_twt_individual_radio_state_t *output);
/* Value-only owner enumeration and close request, safe on the runtime thread.
 * Enumeration writes up to capacity exact tokens; closing_only selects work
 * for the cleanup worker. A close request never calls SDK or drops a lease. */
unsigned esp32_mquickjs_wifi_radio_twt_individual_tokens(esp32_mquickjs_wifi_twt_token_t *tokens,
    unsigned capacity, bool closing_only);
bool esp32_mquickjs_wifi_radio_twt_individual_request_close(const esp32_mquickjs_wifi_twt_token_t *token);
/* Worker only. Cancels pending setup or submits exactly one native teardown
 * for the currently owned established flow, then drives joint retirement.
 * Every error retains token/lease/storage. ESP_OK clears the caller's token.
 * No disconnect of unrelated owners or repeated unconfirmed SDK mutation. */
esp_err_t esp32_mquickjs_wifi_radio_twt_individual_close(esp32_mquickjs_wifi_twt_token_t *token);
/* Worker only; exact live Agreement lease, no implicit driver mutation.
 * A nonzero information identity remains owned even on submission failure. */
esp_err_t esp32_mquickjs_wifi_radio_twt_individual_suspend(const esp32_mquickjs_wifi_twt_token_t *token,
    uint32_t duration_ms, uint32_t *identity);
esp_err_t esp32_mquickjs_wifi_radio_twt_individual_resume(const esp32_mquickjs_wifi_twt_token_t *token, uint32_t *identity);
/* Broadcast IDs 1..31 each admit one stable owner; the pointer directory is
 * lazy and individual owner allocations survive JS/runtime until retirement. */
#define ESP32_MQUICKJS_WIFI_TWT_MAX_BROADCAST_OWNERS 31U
typedef struct {
    esp32_mquickjs_wifi_twt_token_t token;
    wifi_btwt_setup_config_t requested;
    esp32_mquickjs_wifi_btwt_dispatch_t dispatch;
    esp32_mquickjs_wifi_btwt_timer_result_t native;
    esp32_mquickjs_wifi_twt_teardown_tx_snapshot_t teardown;
    esp_err_t submit_error, teardown_error, cleanup_error;
    const char *cleanup_stage;
    uint16_t dialog_attempts_remaining;
    bool dispatching, closing, result_released, native_retired, teardown_attempted;
} esp32_mquickjs_wifi_twt_broadcast_radio_state_t;
esp_err_t esp32_mquickjs_wifi_radio_twt_broadcast_submit(const esp32_mquickjs_wifi_btwt_options_t *,
    esp32_mquickjs_wifi_twt_token_t *);
bool esp32_mquickjs_wifi_radio_twt_broadcast_status(const esp32_mquickjs_wifi_twt_token_t *,
    esp32_mquickjs_wifi_twt_broadcast_radio_state_t *);
unsigned esp32_mquickjs_wifi_radio_twt_broadcast_tokens(esp32_mquickjs_wifi_twt_token_t *, unsigned capacity, bool closing_only);
bool esp32_mquickjs_wifi_radio_twt_broadcast_request_close(const esp32_mquickjs_wifi_twt_token_t *);
esp_err_t esp32_mquickjs_wifi_radio_twt_broadcast_close(esp32_mquickjs_wifi_twt_token_t *);
/* A bounded, exact snapshot. Close requests and enumeration share one lock;
 * owners admitted afterwards are not part of this operation. No driver call,
 * allocation, lease release or borrowed owner pointer. */
typedef struct {
    esp32_mquickjs_wifi_twt_token_t individual[ESP32_MQUICKJS_WIFI_TWT_MAX_INDIVIDUAL];
    esp32_mquickjs_wifi_twt_token_t broadcast[ESP32_MQUICKJS_WIFI_TWT_MAX_BROADCAST_OWNERS];
    unsigned individual_count, broadcast_count;
} esp32_mquickjs_wifi_twt_close_group_t;
void esp32_mquickjs_wifi_radio_twt_close_agreements(esp32_mquickjs_wifi_twt_close_group_t *);
unsigned esp32_mquickjs_wifi_radio_twt_close_pending(const esp32_mquickjs_wifi_twt_close_group_t *);

#endif
