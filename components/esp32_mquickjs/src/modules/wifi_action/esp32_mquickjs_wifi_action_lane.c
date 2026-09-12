#include "esp32_mquickjs_wifi_action_lane.h"
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI
#include "esp_wifi.h"
#include <string.h>

static bool action_exact(const esp32_mquickjs_wifi_action_lane_t *lane,
    const esp32_mquickjs_wifi_action_token_t *token)
{
    return lane != NULL && token != NULL && token->identity != 0U &&
        token->generation == lane->generation && token->identity == lane->identity;
}

esp_err_t esp32_mquickjs_wifi_action_reserve(esp32_mquickjs_wifi_action_lane_t *lane,
    uint32_t generation, esp32_mquickjs_wifi_action_kind_t kind, uint32_t context,
    uint8_t interface, uint8_t channel, esp32_mquickjs_wifi_action_token_t *token)
{
    if (lane == NULL || token == NULL || token->identity != 0U || token->generation != 0U ||
        generation == 0U || context == 0U || interface > WIFI_IF_AP || channel == 0U ||
        (kind != ESP32_MQUICKJS_WIFI_ACTION_SEND && kind != ESP32_MQUICKJS_WIFI_ACTION_ROC)) return ESP_ERR_INVALID_ARG;
    if (lane->identity != 0U) return ESP_ERR_INVALID_STATE;
    if (lane->next_identity == 0U) return ESP_ERR_NO_MEM;
    uint32_t identity = lane->next_identity;
    *lane = (esp32_mquickjs_wifi_action_lane_t){
        .next_identity = identity == UINT32_MAX ? 0U : identity + 1U,
        .generation = generation, .identity = identity, .context = context,
        .kind = kind, .interface = interface, .channel = channel,
        .tx_status = -1, .terminal_status = -1,
    };
    *token = (esp32_mquickjs_wifi_action_token_t){generation, identity};
    return ESP_OK;
}

bool esp32_mquickjs_wifi_action_begin_submit(esp32_mquickjs_wifi_action_lane_t *lane,
    const esp32_mquickjs_wifi_action_token_t *token)
{
    if (!action_exact(lane, token) || lane->dispatching || lane->submitted || lane->cancel_requested || lane->physical_termination) return false;
    lane->dispatching = true;
    return true;
}

/* Every relevant delivery invalidates earlier drain evidence, even duplicates.
 * Terminal conflicts are retained until independent native retirement is proven. */
static void action_record(esp32_mquickjs_wifi_action_lane_t *lane, unsigned status)
{
    lane->sdk_quiescent = lane->sdk_fenced = lane->event_fenced = false;
    if (lane->revision == UINT32_MAX) lane->ambiguous = true;
    else ++lane->revision;
    bool terminal = lane->kind == ESP32_MQUICKJS_WIFI_ACTION_ROC ||
        status == WIFI_ACTION_TX_DURATION_COMPLETED || status == WIFI_ACTION_TX_OP_CANCELLED;
    int8_t *record = terminal ? &lane->terminal_status : &lane->tx_status;
    if (*record != -1 && *record != (int8_t)status) lane->ambiguous = true;
    else *record = (int8_t)status;
    if (terminal) lane->terminal = true;
}

bool esp32_mquickjs_wifi_action_submitted(esp32_mquickjs_wifi_action_lane_t *lane,
    const esp32_mquickjs_wifi_action_token_t *token, esp_err_t error, uint8_t operation_id)
{
    if (!action_exact(lane, token) || !lane->dispatching || lane->submitted) return false;
    lane->dispatching = false;
    lane->submitted = true;
    lane->submit_error = error;
    lane->operation_id = operation_id; /* All 256 SDK IDs, including zero, are valid. */
    for (unsigned i = 0; i < lane->early_count; ++i)
        if (lane->early[i].operation_id == operation_id) action_record(lane, lane->early[i].status);
    lane->early_count = 0;
    memset(lane->early, 0, sizeof(lane->early));
    return true;
}

bool esp32_mquickjs_wifi_action_observe(esp32_mquickjs_wifi_action_lane_t *lane,
    esp32_mquickjs_wifi_action_kind_t kind, uint32_t context, uint8_t interface,
    uint8_t channel, uint8_t operation_id, unsigned status)
{
    if (lane == NULL || lane->identity == 0U || (!lane->dispatching && !lane->submitted) ||
        lane->physical_termination || lane->kind != kind || lane->context != context ||
        lane->channel != channel || (kind == ESP32_MQUICKJS_WIFI_ACTION_SEND && lane->interface != interface)) return false;
    if (lane->submitted && lane->operation_id != operation_id) return false;
    if (status > (kind == ESP32_MQUICKJS_WIFI_ACTION_ROC ? (unsigned)WIFI_ROC_FAIL : (unsigned)WIFI_ACTION_TX_OP_CANCELLED)) {
        lane->ambiguous = true;
        lane->sdk_quiescent = lane->sdk_fenced = lane->event_fenced = false;
        if (lane->revision != UINT32_MAX) ++lane->revision;
        return true;
    }
    if (lane->submitted) {
        action_record(lane, status);
        return true;
    }
    for (unsigned i = 0; i < lane->early_count; ++i)
        if (lane->early[i].operation_id == operation_id && lane->early[i].status == status) return true;
    if (lane->early_count == ESP32_MQUICKJS_WIFI_ACTION_EARLY_EVENTS) lane->ambiguous = true;
    else lane->early[lane->early_count++] = (esp32_mquickjs_wifi_action_early_t){operation_id, (uint8_t)status};
    return true;
}

bool esp32_mquickjs_wifi_action_request_cancel(esp32_mquickjs_wifi_action_lane_t *lane,
    const esp32_mquickjs_wifi_action_token_t *token)
{
    if (!action_exact(lane, token)) return false;
    lane->cancel_requested = true;
    return true;
}

bool esp32_mquickjs_wifi_action_begin_cancel(esp32_mquickjs_wifi_action_lane_t *lane,
    const esp32_mquickjs_wifi_action_token_t *token)
{
    if (!action_exact(lane, token) || !lane->submitted || !lane->cancel_requested || lane->terminal ||
        lane->physical_termination || lane->sdk_quiescent || lane->cancel_busy || lane->cancel_written) return false;
    lane->cancel_busy = true;
    lane->sdk_quiescent = lane->sdk_fenced = lane->event_fenced = false;
    return true;
}

bool esp32_mquickjs_wifi_action_cancelled(esp32_mquickjs_wifi_action_lane_t *lane,
    const esp32_mquickjs_wifi_action_token_t *token, esp_err_t error)
{
    if (!action_exact(lane, token) || !lane->cancel_busy) return false;
    lane->cancel_busy = false;
    lane->cancel_error = error;
    if (error == ESP_OK) lane->cancel_written = true;
    return true;
}

bool esp32_mquickjs_wifi_action_fence_revision(const esp32_mquickjs_wifi_action_lane_t *lane,
    const esp32_mquickjs_wifi_action_token_t *token, uint32_t *revision)
{
    if (!action_exact(lane, token) || revision == NULL || !lane->submitted || (!lane->terminal && !lane->sdk_quiescent) ||
        lane->dispatching || lane->cancel_busy || (lane->ambiguous && !lane->sdk_quiescent)) return false;
    *revision = lane->revision;
    return true;
}

bool esp32_mquickjs_wifi_action_sdk_fenced(esp32_mquickjs_wifi_action_lane_t *lane,
    const esp32_mquickjs_wifi_action_token_t *token, uint32_t revision)
{
    uint32_t current;
    if (!esp32_mquickjs_wifi_action_fence_revision(lane, token, &current) || current != revision) return false;
    lane->sdk_fenced = true;
    return true;
}

bool esp32_mquickjs_wifi_action_event_fenced(esp32_mquickjs_wifi_action_lane_t *lane,
    const esp32_mquickjs_wifi_action_token_t *token, uint32_t revision)
{
    uint32_t current;
    if (!esp32_mquickjs_wifi_action_fence_revision(lane, token, &current) || current != revision || !lane->sdk_fenced) return false;
    lane->event_fenced = true;
    return true;
}

bool esp32_mquickjs_wifi_action_quiescent(esp32_mquickjs_wifi_action_lane_t *lane,
    const esp32_mquickjs_wifi_action_token_t *token, uint32_t revision)
{
    if (!action_exact(lane, token) || !lane->submitted || lane->dispatching || lane->cancel_busy ||
        lane->revision != revision || revision == UINT32_MAX || lane->physical_termination) return false;
    lane->sdk_quiescent = lane->sdk_fenced = true;
    lane->event_fenced = false;
    return true;
}

bool esp32_mquickjs_wifi_action_terminated(esp32_mquickjs_wifi_action_lane_t *lane,
    const esp32_mquickjs_wifi_action_token_t *token)
{
    if (!action_exact(lane, token) || lane->dispatching || lane->cancel_busy) return false;
    lane->physical_termination = true;
    return true;
}

bool esp32_mquickjs_wifi_action_release(esp32_mquickjs_wifi_action_lane_t *lane,
    esp32_mquickjs_wifi_action_token_t *token)
{
    if (!action_exact(lane, token) || lane->dispatching || lane->cancel_busy) return false;
    if (lane->submitted && !lane->physical_termination &&
        (((!lane->terminal || lane->ambiguous) && !lane->sdk_quiescent) || !lane->sdk_fenced || !lane->event_fenced)) return false;
    uint32_t next = lane->next_identity;
    memset(lane, 0, sizeof(*lane));
    lane->next_identity = next;
    *token = (esp32_mquickjs_wifi_action_token_t){0};
    return true;
}
#endif
