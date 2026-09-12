#include "esp32_mquickjs_wifi_interval.h"
#if CONFIG_ESP32_MQUICKJS_WIFI_RADIO
#include <stddef.h>
#include <string.h>

static bool interval_result(esp32_mquickjs_wifi_interval_result_t *result)
{
    if (result == NULL) return false;
    *result = (esp32_mquickjs_wifi_interval_result_t){.error = ESP_ERR_INVALID_ARG};
    return true;
}

static bool interval_token_valid(const esp32_mquickjs_wifi_interval_state_t *state,
    const esp32_mquickjs_wifi_interval_token_t *token)
{
    return state != NULL && token != NULL && token->generation != 0U &&
        token->owner_identity != 0U && token->identity != 0U &&
        state->generation == token->generation && state->owner.generation == token->generation &&
        state->owner.owner_identity == token->owner_identity && state->owner.identity == token->identity &&
        state->restore_pending;
}

static esp_err_t interval_apply(esp32_mquickjs_wifi_interval_state_t *state, uint16_t value, bool restoring,
    esp32_mquickjs_wifi_interval_writer_t writer, void *opaque,
    esp32_mquickjs_wifi_interval_result_t *result)
{
    if (state->revision == UINT32_MAX) return result->error = ESP_ERR_INVALID_STATE;
    result->revision = ++state->revision;
    result->attempted = true;
    esp_err_t err = writer(opaque, value);
    if (restoring) state->restore_error = err;
    else { state->error = err; state->restore_error = ESP_OK; }
    state->known = err == ESP_OK;
    state->uncertain = err != ESP_OK;
    if (err == ESP_OK) state->value = value;
    result->accepted = err == ESP_OK;
    result->error = err;
    return err;
}

esp_err_t esp32_mquickjs_wifi_interval_write(esp32_mquickjs_wifi_interval_state_t *state,
    uint32_t generation, uint16_t milliseconds, esp32_mquickjs_wifi_interval_writer_t writer,
    void *opaque, esp32_mquickjs_wifi_interval_result_t *result)
{
    if (!interval_result(result) || state == NULL || generation == 0U || writer == NULL)
        return ESP_ERR_INVALID_ARG;
    if (state->owner.identity != 0U || state->restore_pending ||
        (state->generation != 0U && state->generation != generation) || state->revision == UINT32_MAX)
        return result->error = ESP_ERR_INVALID_STATE;
    state->generation = generation;
    /* Explicit replacement can establish knowledge after an unowned failed
     * write. It does not pretend to restore any unknown previous value. */
    return interval_apply(state, milliseconds, false, writer, opaque, result);
}

esp_err_t esp32_mquickjs_wifi_interval_acquire(esp32_mquickjs_wifi_interval_state_t *state,
    uint32_t generation, uint32_t owner_identity, uint16_t milliseconds,
    esp32_mquickjs_wifi_interval_writer_t writer, void *opaque,
    esp32_mquickjs_wifi_interval_token_t *token, esp32_mquickjs_wifi_interval_result_t *result)
{
    if (!interval_result(result) || state == NULL || token == NULL || generation == 0U ||
        owner_identity == 0U || writer == NULL) return ESP_ERR_INVALID_ARG;
    /* Do not erase a caller's live token on rejected reacquisition. */
    if (token->identity != 0U || token->generation != 0U || token->owner_identity != 0U ||
        state->generation != generation || !state->known || state->uncertain ||
        state->owner.identity != 0U || state->restore_pending || state->revision > UINT32_MAX - 2U)
        return result->error = ESP_ERR_INVALID_STATE;
    state->previous = state->value;
    state->owner = (esp32_mquickjs_wifi_interval_token_t){generation, owner_identity, state->revision + 1U};
    *token = state->owner;
    state->restore_pending = true;
    /* Publish ownership before SDK mutation, including a setter that changes
     * the driver and then returns an error. */
    return interval_apply(state, milliseconds, false, writer, opaque, result);
}

esp_err_t esp32_mquickjs_wifi_interval_update(esp32_mquickjs_wifi_interval_state_t *state,
    const esp32_mquickjs_wifi_interval_token_t *token, uint16_t milliseconds,
    esp32_mquickjs_wifi_interval_writer_t writer, void *opaque,
    esp32_mquickjs_wifi_interval_result_t *result)
{
    if (!interval_result(result) || state == NULL || token == NULL || writer == NULL)
        return ESP_ERR_INVALID_ARG;
    if (!interval_token_valid(state, token) || !state->known || state->uncertain ||
        state->revision > UINT32_MAX - 2U) return result->error = ESP_ERR_INVALID_STATE;
    /* The first captured previous value survives every successful update. */
    return interval_apply(state, milliseconds, false, writer, opaque, result);
}

esp_err_t esp32_mquickjs_wifi_interval_release(esp32_mquickjs_wifi_interval_state_t *state,
    esp32_mquickjs_wifi_interval_token_t *token, esp32_mquickjs_wifi_interval_writer_t writer,
    void *opaque, esp32_mquickjs_wifi_interval_result_t *result)
{
    if (!interval_result(result) || state == NULL || token == NULL || writer == NULL)
        return ESP_ERR_INVALID_ARG;
    /* A consumed token is idempotent even if a different owner has since joined.
     * It never clears that owner's state or performs an SDK call. */
    if (token->identity == 0U && token->generation == 0U && token->owner_identity == 0U) {
        result->error = ESP_OK;
        return ESP_OK;
    }
    if (!interval_token_valid(state, token)) return result->error = ESP_ERR_INVALID_STATE;
    esp_err_t err = interval_apply(state, state->previous, true, writer, opaque, result);
    if (err != ESP_OK) return err;
    state->owner = (esp32_mquickjs_wifi_interval_token_t){0};
    state->previous = 0;
    state->restore_pending = false;
    *token = (esp32_mquickjs_wifi_interval_token_t){0};
    return ESP_OK;
}

esp_err_t esp32_mquickjs_wifi_interval_capture(const esp32_mquickjs_wifi_interval_state_t *state,
    uint32_t generation, esp32_mquickjs_wifi_interval_snapshot_t *snapshot)
{
    if (snapshot == NULL) return ESP_ERR_INVALID_ARG;
    *snapshot = (esp32_mquickjs_wifi_interval_snapshot_t){0};
    if (state == NULL || generation == 0U) return ESP_ERR_INVALID_ARG;
    if (state->generation != generation || !state->known || state->uncertain ||
        state->owner.identity != 0U || state->restore_pending || state->revision == 0U ||
        state->revision > UINT32_MAX - 2U) return ESP_ERR_INVALID_STATE;
    *snapshot = (esp32_mquickjs_wifi_interval_snapshot_t){generation, state->revision, state->value};
    return ESP_OK;
}

esp_err_t esp32_mquickjs_wifi_interval_replay(esp32_mquickjs_wifi_interval_state_t *state,
    uint32_t generation, const esp32_mquickjs_wifi_interval_snapshot_t *snapshot,
    esp32_mquickjs_wifi_interval_writer_t writer, void *opaque,
    esp32_mquickjs_wifi_interval_result_t *result)
{
    if (!interval_result(result) || state == NULL || snapshot == NULL || generation == 0U || writer == NULL)
        return ESP_ERR_INVALID_ARG;
    if (snapshot->generation == 0U || snapshot->generation == generation || snapshot->revision == 0U ||
        state->generation != generation || !state->known || state->uncertain ||
        state->owner.identity != 0U || state->restore_pending || state->revision <= snapshot->revision)
        return result->error = ESP_ERR_INVALID_STATE;
    return esp32_mquickjs_wifi_interval_write(state, generation, snapshot->value, writer, opaque, result);
}

bool esp32_mquickjs_wifi_interval_invalidate(esp32_mquickjs_wifi_interval_state_t *state,
    uint32_t generation)
{
    if (state == NULL || generation == 0U || state->owner.identity != 0U || state->restore_pending ||
        (state->generation != 0U && state->generation != generation)) return false;
    uint32_t revision = state->revision;
    memset(state, 0, sizeof(*state));
    state->revision = revision;
    return true;
}
#endif
