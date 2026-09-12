#include "esp32_mquickjs_wifi_twt_broadcast_retire.h"
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI && CONFIG_SOC_WIFI_HE_SUPPORT && CONFIG_IDF_TARGET_ESP32C5
static esp_err_t btwt_retire_error(esp32_mquickjs_wifi_btwt_retire_t *state, esp_err_t error)
{
    state->error = error;
    return error;
}
static esp_err_t btwt_retire_clear(esp32_mquickjs_wifi_btwt_retire_t *state)
{
    state->stage = "broadcast-marker-clear";
    if (state->fence.token.identity == 0) return ESP_OK;
    return esp32_mquickjs_wifi_twt_fence_clear(&state->fence, &state->token, state->cut.tx_revision);
}
esp_err_t esp32_mquickjs_wifi_btwt_retire_poll(esp32_mquickjs_wifi_btwt_retire_t *state,
    const esp32_mquickjs_wifi_twt_token_t *token, unsigned slot, uint32_t identity)
{
    if (state == NULL || token == NULL || !token->identity || !token->generation || slot >= 32 || !identity)
        return ESP_ERR_INVALID_ARG;
    if (state->identity != 0 && (state->identity != identity || state->slot != slot ||
        state->token.identity != token->identity || state->token.generation != token->generation)) return ESP_ERR_INVALID_STATE;
    if (state->identity == 0) { state->identity = identity; state->slot = slot; state->token = *token; }
    if (state->released) {
        esp_err_t error = btwt_retire_clear(state);
        if (error == ESP_OK) state->stage = NULL;
        return btwt_retire_error(state, error);
    }
    if (state->fence.start_error != ESP_OK || state->fence.clearing) {
        esp_err_t error = btwt_retire_clear(state);
        if (error != ESP_OK) return btwt_retire_error(state, error);
        state->sequence = 0;
    }
    esp32_mquickjs_wifi_twt_teardown_tx_snapshot_t teardown;
    esp32_mquickjs_wifi_twt_teardown_tx_snapshot(&teardown);
    if (teardown.broadcast && teardown.identity == identity && teardown.flow == slot)
        state->teardown = teardown;
    state->stage = "broadcast-cancel";
    esp_err_t error = esp32_mquickjs_wifi_twt_sdk_broadcast_cancel(slot, identity);
    if (error != ESP_OK) return btwt_retire_error(state, error);
    esp32_mquickjs_wifi_btwt_cut_t current;
    state->stage = "broadcast-quiescence";
    error = esp32_mquickjs_wifi_twt_sdk_broadcast_quiescent(slot, identity, &current);
    if (error != ESP_OK) return btwt_retire_error(state, error);
    if (state->fence.token.identity != 0 && (state->cut.tx_revision != current.tx_revision ||
        state->cut.timer_revision != current.timer_revision ||
        state->cut.teardown_revision != current.teardown_revision)) {
        error = btwt_retire_clear(state);
        if (error == ESP_OK) { state->sequence = 0; error = ESP_ERR_NOT_FINISHED; }
        return btwt_retire_error(state, error);
    }
    if (state->fence.token.identity == 0) {
        state->cut = current;
        state->stage = "broadcast-marker-start";
        error = esp32_mquickjs_wifi_twt_fence_begin(&state->fence, &state->token, state->cut.tx_revision);
        if (error != ESP_OK) return btwt_retire_error(state, error);
    }
    state->stage = "broadcast-timer-native-fence";
    error = esp32_mquickjs_wifi_twt_fence_poll(&state->fence, &state->token, state->cut.tx_revision);
    if (error != ESP_OK) return btwt_retire_error(state, error);
    if (state->sequence == 0) {
        state->stage = "broadcast-event-post";
        error = esp32_mquickjs_wifi_btwt_setup_post_fence(slot, identity, &state->cut, &state->sequence);
        if (error != ESP_OK) return btwt_retire_error(state, error);
    }
    esp32_mquickjs_wifi_twt_teardown_tx_snapshot(&teardown);
    if (teardown.broadcast && teardown.identity == identity && teardown.flow == slot)
        state->teardown = teardown;
    state->stage = "broadcast-release";
    error = esp32_mquickjs_wifi_twt_sdk_broadcast_release(slot, identity, &state->cut, state->sequence);
    if (error != ESP_OK) return btwt_retire_error(state, error);
    state->released = true;
    error = btwt_retire_clear(state);
    if (error == ESP_OK) state->stage = NULL;
    return btwt_retire_error(state, error);
}
#endif
