#include "esp32_mquickjs_wifi_twt_probe_retire.h"
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI && CONFIG_SOC_WIFI_HE_SUPPORT && CONFIG_IDF_TARGET_ESP32C5

static esp_err_t probe_retire_result(esp32_mquickjs_wifi_twt_probe_retire_t *state, esp_err_t error)
{
    state->error = error;
    return error;
}
static esp_err_t probe_retire_clear_marker(esp32_mquickjs_wifi_twt_probe_retire_t *state)
{
    if (state->fence.token.identity == 0U) return ESP_OK;
    return esp32_mquickjs_wifi_twt_fence_clear(&state->fence, &state->token, state->cut.tx_revision);
}
esp_err_t esp32_mquickjs_wifi_twt_probe_retire_poll(esp32_mquickjs_wifi_twt_probe_retire_t *state,
    const esp32_mquickjs_wifi_twt_token_t *token, uint32_t identity)
{
    if (state == NULL || token == NULL || token->identity == 0U || token->generation == 0U || identity == 0U)
        return ESP_ERR_INVALID_ARG;
    if (state->identity == 0U) {
        esp32_mquickjs_wifi_twt_probe_result_snapshot_t result;
        esp32_mquickjs_wifi_twt_probe_result_snapshot(&result);
        if (result.identity != identity || !result.owned) return ESP_ERR_INVALID_STATE;
        state->token = *token;
        state->identity = identity;
    }
    if (state->identity != identity || state->token.identity != token->identity || state->token.generation != token->generation)
        return ESP_ERR_INVALID_STATE;
    if (state->released) {
        state->stage = "probe-marker-clear";
        esp_err_t error = probe_retire_clear_marker(state);
        if (error == ESP_OK) state->stage = NULL;
        return probe_retire_result(state, error);
    }
    if (!state->cancelled) {
        state->stage = "probe-cancel";
        esp_err_t error = esp32_mquickjs_wifi_twt_sdk_probe_cancel(identity);
        if (error != ESP_OK) return probe_retire_result(state, error);
        state->cancelled = true;
    }
    /* A failed marker owns its callback storage until clear succeeds. Clear
     * is also independent of Wi-Fi availability, so retry it before querying
     * the driver again. The native probe pin remains owned during this retry. */
    if (state->fence.start_error != ESP_OK || state->fence.clearing) {
        state->stage = "probe-marker-clear";
        esp_err_t error = probe_retire_clear_marker(state);
        if (error != ESP_OK) return probe_retire_result(state, error);
        state->event_sequence = 0;
    }
    esp32_mquickjs_wifi_twt_probe_cut_t current;
    state->stage = "probe-native-quiescence";
    esp_err_t error = esp32_mquickjs_wifi_twt_sdk_probe_quiescent(identity, &current);
    if (error != ESP_OK) return probe_retire_result(state, error);
    if (current.tx_revision == UINT32_MAX) return probe_retire_result(state, ESP_ERR_NO_MEM);
    if (state->fence.token.identity != 0U &&
        (current.tx_revision != state->cut.tx_revision || current.timer_identity != state->cut.timer_identity)) {
        /* Even unrelated management TX changes conservatively require a new
         * ordering point. A queued old event carries its previous sequence. */
        state->stage = "probe-marker-clear";
        error = probe_retire_clear_marker(state);
        if (error != ESP_OK) return probe_retire_result(state, error);
        state->event_sequence = 0;
        return probe_retire_result(state, ESP_ERR_NOT_FINISHED);
    }
    if (state->fence.token.identity == 0U) {
        state->cut = current;
        state->stage = "probe-marker-start";
        error = esp32_mquickjs_wifi_twt_fence_begin(&state->fence, &state->token, state->cut.tx_revision);
        if (error != ESP_OK) return probe_retire_result(state, error);
    }
    state->stage = "probe-timer-native-fence";
    error = esp32_mquickjs_wifi_twt_fence_poll(&state->fence, &state->token, state->cut.tx_revision);
    if (error != ESP_OK) return probe_retire_result(state, error);
    /* Release rechecks the cut on the native queue after event delivery.
     * A changed cut is detected at the next poll and invalidates this marker. */
    if (state->event_sequence == 0U) {
        state->stage = "probe-event-post";
        error = esp32_mquickjs_wifi_twt_probe_result_post_fence(identity, &state->event_sequence);
        if (error != ESP_OK) return probe_retire_result(state, error);
    }
    state->stage = "probe-native-release";
    error = esp32_mquickjs_wifi_twt_sdk_probe_release(identity, &state->cut, state->event_sequence);
    if (error != ESP_OK) return probe_retire_result(state, error);
    state->released = true;
    state->stage = "probe-marker-clear";
    error = probe_retire_clear_marker(state);
    if (error == ESP_OK) state->stage = NULL;
    return probe_retire_result(state, error);
}
#endif
