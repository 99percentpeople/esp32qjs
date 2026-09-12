#include "esp32_mquickjs_wifi_twt_setup_retire.h"
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI && CONFIG_SOC_WIFI_HE_SUPPORT && CONFIG_IDF_TARGET_ESP32C5

static esp_err_t setup_retire_result(esp32_mquickjs_wifi_twt_setup_retire_t *state, esp_err_t error)
{
    state->error = error;
    return error;
}
static esp_err_t setup_retire_clear_marker(esp32_mquickjs_wifi_twt_setup_retire_t *state)
{
    if (state->fence.token.identity == 0U) return ESP_OK;
    return esp32_mquickjs_wifi_twt_fence_clear(&state->fence, &state->token, state->cut.tx_revision);
}
static esp_err_t setup_retire_finish(esp32_mquickjs_wifi_twt_setup_retire_t *state)
{
    if (!state->released && state->cut.teardown_revision != 0U) {
        state->stage = "teardown-tx-release";
        esp_err_t error = esp32_mquickjs_wifi_twt_sdk_teardown_tx_release(state->identity, state->cut.teardown_revision);
        if (error != ESP_OK) return setup_retire_result(state, error);
    }
    state->released = true;
    state->stage = "setup-marker-clear";
    esp_err_t error = setup_retire_clear_marker(state);
    if (error == ESP_OK) state->stage = NULL;
    return setup_retire_result(state, error);
}
esp_err_t esp32_mquickjs_wifi_twt_setup_retire_poll(esp32_mquickjs_wifi_twt_setup_retire_t *state,
    const esp32_mquickjs_wifi_twt_token_t *token, uint32_t identity)
{
    if (state == NULL || token == NULL || token->identity == 0U || token->generation == 0U || identity == 0U)
        return ESP_ERR_INVALID_ARG;
    if (state->identity != 0U && (state->identity != identity || state->token.identity != token->identity ||
        state->token.generation != token->generation)) return ESP_ERR_INVALID_STATE;
    if (state->result_released) return setup_retire_finish(state);
    esp32_mquickjs_wifi_twt_setup_result_t result;
    esp_err_t error = esp32_mquickjs_wifi_twt_setup_result_read(identity, &result);
    if (error != ESP_OK) return error;
    if (state->identity == 0U) { state->identity = identity; state->token = *token; }
    /* Retire a failed marker before touching the driver. A native observation
     * after cancellation revokes both cancellation and the old event proof. */
    if (state->fence.start_error != ESP_OK || state->fence.clearing ||
        !(result.flags & ESP32_MQUICKJS_WIFI_TWT_SETUP_CANCELLED)) {
        state->stage = "setup-marker-clear";
        error = setup_retire_clear_marker(state);
        if (error != ESP_OK) return setup_retire_result(state, error);
        state->event_sequence = 0;
    }
    if (!(result.flags & ESP32_MQUICKJS_WIFI_TWT_SETUP_CANCELLED)) {
        state->stage = "setup-cancel";
        error = esp32_mquickjs_wifi_twt_sdk_setup_cancel(identity);
        if (error != ESP_OK) return setup_retire_result(state, error);
    }
    esp32_mquickjs_wifi_twt_setup_cut_t current;
    state->stage = "setup-native-quiescence";
    error = esp32_mquickjs_wifi_twt_sdk_setup_quiescent(identity, &current);
    if (error != ESP_OK) return setup_retire_result(state, error);
    if (state->fence.token.identity != 0U && (current.tx_revision != state->cut.tx_revision ||
        current.timer_revision != state->cut.timer_revision ||
        current.information_revision != state->cut.information_revision ||
        current.teardown_revision != state->cut.teardown_revision)) {
        state->stage = "setup-marker-clear";
        error = setup_retire_clear_marker(state);
        if (error != ESP_OK) return setup_retire_result(state, error);
        state->event_sequence = 0;
        return setup_retire_result(state, ESP_ERR_NOT_FINISHED);
    }
    if (state->fence.token.identity == 0U) {
        state->cut = current;
        state->stage = "setup-marker-start";
        error = esp32_mquickjs_wifi_twt_fence_begin(&state->fence, &state->token, state->cut.tx_revision);
        if (error != ESP_OK) return setup_retire_result(state, error);
    }
    state->stage = "setup-timer-native-fence";
    error = esp32_mquickjs_wifi_twt_fence_poll(&state->fence, &state->token, state->cut.tx_revision);
    if (error != ESP_OK) return setup_retire_result(state, error);
    state->stage = "setup-event-read";
    error = esp32_mquickjs_wifi_twt_setup_result_read(identity, &result);
    if (error != ESP_OK) return setup_retire_result(state, error);
    if (!(result.flags & ESP32_MQUICKJS_WIFI_TWT_SETUP_CANCELLED)) return setup_retire_result(state, ESP_ERR_NOT_FINISHED);
    if (state->event_sequence != result.fence_sequence ||
        !(result.flags & ESP32_MQUICKJS_WIFI_TWT_SETUP_FENCE_POSTED)) state->event_sequence = 0;
    if (state->event_sequence == 0U) {
        state->stage = "setup-event-post";
        error = esp32_mquickjs_wifi_twt_setup_result_post_fence(identity, result.revision, &state->event_sequence);
        if (error != ESP_OK) return setup_retire_result(state, error);
    }
    state->stage = "setup-native-release";
    error = esp32_mquickjs_wifi_twt_sdk_setup_release(identity, &state->cut, state->event_sequence);
    if (error != ESP_OK) return setup_retire_result(state, error);
    /* The result record is gone, but the native TX scope still blocks reuse.
     * Keep stable owner storage and retry only the remaining release suffix. */
    state->result_released = true;
    return setup_retire_finish(state);
}
#endif
