#pragma once
#include "esp32_mquickjs_wifi_twt_probe_retire.h"
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI && CONFIG_SOC_WIFI_HE_SUPPORT && CONFIG_IDF_TARGET_ESP32C5
/* Worker-only, serialized with lifecycle/mutations. A read acquires no lease,
 * submits no RF request and retains no native storage after the call returns.
 * Both output values are committed only on success. */
esp_err_t esp32_mquickjs_wifi_radio_twt_broadcast_snapshot(
    esp32_mquickjs_wifi_twt_broadcast_snapshot_t *output, uint32_t *generation);
typedef struct {
    esp32_mquickjs_wifi_twt_token_t token;
    uint32_t native_identity;
    esp_err_t submit_error, native_error, cleanup_error;
    const char *cleanup_stage;
    bool dispatching, cleanup_pending;
    esp32_mquickjs_wifi_twt_probe_result_snapshot_t native;
} esp32_mquickjs_wifi_twt_probe_radio_state_t;
/* Already started, associated Station only; no implicit PS/mode/channel change.
 * The exact Radio lease and fixed home channel survive errors/public timeout.
 * Nonzero token on error must be retired. SDK submission is never retried. */
esp_err_t esp32_mquickjs_wifi_radio_twt_probe_submit(uint32_t response_ms,
    esp32_mquickjs_wifi_twt_token_t *token);
/* Native copies only; no SDK call or Radio mutation mutex on the reader. */
bool esp32_mquickjs_wifi_radio_twt_probe_status(const esp32_mquickjs_wifi_twt_token_t *token,
    esp32_mquickjs_wifi_twt_probe_radio_state_t *output);
void esp32_mquickjs_wifi_radio_twt_probe_snapshot(esp32_mquickjs_wifi_twt_probe_radio_state_t *output);
/* Value-only cleanup handoff, including an admitted generation recovery.
 * The exact owner remains in Radio; no Future/native pointer is transferred. */
bool esp32_mquickjs_wifi_radio_twt_probe_request_close(const esp32_mquickjs_wifi_twt_token_t *token);
bool esp32_mquickjs_wifi_radio_twt_probe_cleanup_token(esp32_mquickjs_wifi_twt_token_t *token);
/* Outside runtime/event/timer tasks. Runs one joint cleanup step under the
 * mutation mutex; no driver call inside the snapshot critical section.
 * NOT_FINISHED retains the exact owner. ESP_OK releases lease and clears token. */
esp_err_t esp32_mquickjs_wifi_radio_twt_probe_retire(esp32_mquickjs_wifi_twt_token_t *token);
#endif
