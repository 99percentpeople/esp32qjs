#include "esp32_mquickjs_wifi_policy.h"
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI
#include <stddef.h>

esp_err_t esp32_mquickjs_wifi_policy_apply(esp32_mquickjs_wifi_policy_state_t *state,
    uint32_t generation, esp32_mquickjs_wifi_policy_slot_t slot, bool requested,
    esp32_mquickjs_wifi_policy_writer_t writer, void *opaque,
    esp32_mquickjs_wifi_policy_write_t *result)
{
    if (result == NULL) return ESP_ERR_INVALID_ARG;
    *result = (esp32_mquickjs_wifi_policy_write_t){.error = ESP_ERR_INVALID_ARG};
    if (state == NULL || generation == 0U || writer == NULL ||
        (unsigned)slot >= ESP32_MQUICKJS_WIFI_POLICY_SLOT_COUNT) return result->error;
    result->error = ESP_ERR_INVALID_STATE;
    if (state->revision == UINT32_MAX) return result->error;
    for (unsigned i = 0; i < ESP32_MQUICKJS_WIFI_POLICY_SLOT_COUNT; ++i) {
        const esp32_mquickjs_wifi_policy_record_t *record = &state->records[i];
        if ((record->known || record->uncertain) && record->generation != generation)
            return result->error;
    }
    esp32_mquickjs_wifi_policy_record_t *record = &state->records[slot];
    record->generation = generation;
    record->revision = ++state->revision;
    record->requested = requested;
    record->known = false;
    record->uncertain = true;
    result->revision = record->revision;
    result->attempted = true;
    esp_err_t err = writer(opaque, slot, requested);
    record->error = result->error = err;
    if (err == ESP_OK) {
        record->accepted_revision = record->revision;
        record->value = requested;
        record->configured = true;
        record->known = true;
        record->uncertain = false;
        result->accepted = true;
    }
    return err;
}

void esp32_mquickjs_wifi_policy_invalidate(esp32_mquickjs_wifi_policy_state_t *state)
{
    if (state == NULL) return;
    for (unsigned i = 0; i < ESP32_MQUICKJS_WIFI_POLICY_SLOT_COUNT; ++i) {
        state->records[i].known = false;
        state->records[i].uncertain = false;
    }
}

esp_err_t esp32_mquickjs_wifi_policy_capture(const esp32_mquickjs_wifi_policy_state_t *state,
    uint32_t generation, esp32_mquickjs_wifi_policy_snapshot_t *snapshot)
{
    if (snapshot == NULL) return ESP_ERR_INVALID_ARG;
    *snapshot = (esp32_mquickjs_wifi_policy_snapshot_t){0};
    if (state == NULL || generation == 0U) return ESP_ERR_INVALID_ARG;
    esp32_mquickjs_wifi_policy_snapshot_t captured = {.generation = generation, .revision = state->revision};
    unsigned count = 0;
    for (unsigned i = 0; i < ESP32_MQUICKJS_WIFI_POLICY_SLOT_COUNT; ++i) {
        const esp32_mquickjs_wifi_policy_record_t *record = &state->records[i];
        if (record->uncertain || (record->configured && (!record->known || record->generation != generation)))
            return ESP_ERR_INVALID_STATE;
        if (!record->configured) continue;
        captured.mask |= 1U << i;
        if (record->value) captured.values |= 1U << i;
        ++count;
    }
    /* Reserve space for one complete replay before destroying the old driver.
     * Further failed attempts can still exhaust this boot's revision space. */
    if (count > UINT32_MAX - state->revision) return ESP_ERR_INVALID_STATE;
    *snapshot = captured;
    return ESP_OK;
}

esp_err_t esp32_mquickjs_wifi_policy_replay(esp32_mquickjs_wifi_policy_state_t *state,
    uint32_t generation, const esp32_mquickjs_wifi_policy_snapshot_t *snapshot,
    bool after_start, uint8_t *completed, esp32_mquickjs_wifi_policy_writer_t writer,
    void *opaque, esp32_mquickjs_wifi_policy_write_t *result)
{
    if (result == NULL) return ESP_ERR_INVALID_ARG;
    *result = (esp32_mquickjs_wifi_policy_write_t){.error = ESP_ERR_INVALID_ARG};
    const uint8_t all = (1U << ESP32_MQUICKJS_WIFI_POLICY_SLOT_COUNT) - 1U;
    const uint8_t post = (1U << ESP32_MQUICKJS_WIFI_POLICY_SLOT_DYNAMIC_CS)
#if CONFIG_SOC_WIFI_HE_SUPPORT
        | (1U << ESP32_MQUICKJS_WIFI_POLICY_SLOT_BSS_COLOR)
#endif
        ;
    if (state == NULL || snapshot == NULL || completed == NULL || writer == NULL ||
        generation == 0U || snapshot->generation == 0U ||
        (snapshot->mask & ~all) || (snapshot->values & ~snapshot->mask) || (*completed & ~snapshot->mask))
        return result->error;
    result->error = ESP_ERR_INVALID_STATE;
    if ((snapshot->mask != 0U && generation == snapshot->generation) || snapshot->revision > state->revision ||
        (after_start && (*completed & (snapshot->mask & ~post)) != (snapshot->mask & ~post))) return result->error;
    uint8_t pending = snapshot->mask & ~*completed & (after_start ? post : (uint8_t)~post);
    for (unsigned i = 0; i < ESP32_MQUICKJS_WIFI_POLICY_SLOT_COUNT; ++i) {
        uint8_t bit = 1U << i;
        if (!(pending & bit)) continue;
        esp_err_t err = esp32_mquickjs_wifi_policy_apply(state, generation, i,
            (snapshot->values & bit) != 0U, writer, opaque, result);
        if (err != ESP_OK) return err;
        *completed |= bit;
    }
    result->error = ESP_OK;
    return ESP_OK;
}
#endif
