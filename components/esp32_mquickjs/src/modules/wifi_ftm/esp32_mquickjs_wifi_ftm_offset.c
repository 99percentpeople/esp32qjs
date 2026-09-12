#include "esp32_mquickjs_wifi_ftm_offset.h"
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI && CONFIG_ESP_WIFI_FTM_ENABLE && CONFIG_ESP_WIFI_FTM_RESPONDER_SUPPORT && CONFIG_ESP_WIFI_SOFTAP_SUPPORT
#include <stddef.h>

esp_err_t esp32_mquickjs_wifi_ftm_offset_apply(esp32_mquickjs_wifi_ftm_offset_state_t *state,
    uint32_t generation, int16_t centimeters, esp32_mquickjs_wifi_ftm_offset_writer_t writer,
    void *opaque, bool *attempted)
{
    if (attempted == NULL) return ESP_ERR_INVALID_ARG;
    *attempted = false;
    if (state == NULL || generation == 0U || writer == NULL) return ESP_ERR_INVALID_ARG;
    if (state->revision == UINT32_MAX ||
        ((state->known || state->uncertain) && state->generation != generation)) return ESP_ERR_INVALID_STATE;
    state->generation = generation;
    ++state->revision;
    state->requested_cm = centimeters;
    state->known = false;
    state->uncertain = true;
    *attempted = true;
    state->error = writer(opaque, centimeters);
    if (state->error == ESP_OK) {
        state->accepted_revision = state->revision;
        state->accepted_cm = centimeters;
        state->configured = state->known = true;
        state->uncertain = false;
    }
    return state->error;
}

void esp32_mquickjs_wifi_ftm_offset_invalidate(esp32_mquickjs_wifi_ftm_offset_state_t *state)
{
    if (state == NULL) return;
    state->known = state->uncertain = false;
}

esp_err_t esp32_mquickjs_wifi_ftm_offset_capture(const esp32_mquickjs_wifi_ftm_offset_state_t *state,
    uint32_t generation, esp32_mquickjs_wifi_ftm_offset_snapshot_t *snapshot)
{
    if (snapshot == NULL) return ESP_ERR_INVALID_ARG;
    *snapshot = (esp32_mquickjs_wifi_ftm_offset_snapshot_t){0};
    if (state == NULL || generation == 0U) return ESP_ERR_INVALID_ARG;
    if (state->uncertain || (state->configured &&
        (!state->known || state->generation != generation || state->revision == UINT32_MAX)))
        return ESP_ERR_INVALID_STATE;
    *snapshot = (esp32_mquickjs_wifi_ftm_offset_snapshot_t){.generation = generation,
        .revision = state->revision, .centimeters = state->accepted_cm, .configured = state->configured};
    return ESP_OK;
}

esp_err_t esp32_mquickjs_wifi_ftm_offset_replay(esp32_mquickjs_wifi_ftm_offset_state_t *state,
    uint32_t generation, const esp32_mquickjs_wifi_ftm_offset_snapshot_t *snapshot,
    bool *completed, esp32_mquickjs_wifi_ftm_offset_writer_t writer, void *opaque)
{
    if (state == NULL || snapshot == NULL || completed == NULL || writer == NULL ||
        generation == 0U || snapshot->generation == 0U) return ESP_ERR_INVALID_ARG;
    if (snapshot->revision > state->revision ||
        (snapshot->configured && generation == snapshot->generation)) return ESP_ERR_INVALID_STATE;
    if (!snapshot->configured) { *completed = true; return ESP_OK; }
    if (*completed) return state->known && !state->uncertain && state->generation == generation &&
        state->accepted_cm == snapshot->centimeters ? ESP_OK : ESP_ERR_INVALID_STATE;
    bool attempted;
    esp_err_t err = esp32_mquickjs_wifi_ftm_offset_apply(state, generation, snapshot->centimeters,
        writer, opaque, &attempted);
    if (err == ESP_OK) *completed = true;
    return err;
}
#endif
