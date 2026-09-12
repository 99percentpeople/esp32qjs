#include "esp32_mquickjs_wifi_twt_lane.h"
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI && CONFIG_SOC_WIFI_HE_SUPPORT
#include <string.h>

static bool twt_exact(const esp32_mquickjs_wifi_twt_lane_t *lane,
    const esp32_mquickjs_wifi_twt_token_t *token)
{
    return lane != NULL && token != NULL && token->identity != 0U &&
        token->identity == lane->token.identity && token->generation == lane->token.generation;
}
static esp_err_t twt_admit(const esp32_mquickjs_wifi_twt_identity_t *domain,
    const esp32_mquickjs_wifi_twt_lane_t *lane, uint32_t generation,
    const esp32_mquickjs_wifi_twt_token_t *token)
{
    if (domain == NULL || lane == NULL || token == NULL || generation == 0U ||
        token->identity != 0U || token->generation != 0U) return ESP_ERR_INVALID_ARG;
    if (lane->token.identity != 0U) return ESP_ERR_INVALID_STATE;
    return domain->next_identity != 0U ? ESP_OK : ESP_ERR_NO_MEM;
}
static void twt_reserve(esp32_mquickjs_wifi_twt_identity_t *domain,
    esp32_mquickjs_wifi_twt_lane_t *lane, uint32_t generation, unsigned kind,
    uint32_t timeout_ms, esp32_mquickjs_wifi_twt_token_t *token)
{
    uint64_t identity = domain->next_identity;
    domain->next_identity = identity == UINT64_MAX ? 0U : identity + 1U;
    *lane = (esp32_mquickjs_wifi_twt_lane_t){
        .token = {.identity = identity, .generation = generation},
        .kind = kind, .timeout_ms = timeout_ms, .teardown_status = -1,
    };
    *token = lane->token;
}
esp_err_t esp32_mquickjs_wifi_twt_reserve_individual(esp32_mquickjs_wifi_twt_identity_t *domain,
    esp32_mquickjs_wifi_twt_lane_t *lane, uint32_t generation,
    const esp32_mquickjs_wifi_itwt_options_t *options, esp32_mquickjs_wifi_twt_token_t *token)
{
    if (!esp32_mquickjs_wifi_itwt_options_valid(options)) return ESP_ERR_INVALID_ARG;
    esp_err_t error = twt_admit(domain, lane, generation, token);
    if (error != ESP_OK) return error;
    uint32_t connection = options->connection_id_set ? options->config.twt_id : domain->next_connection_id;
    if (domain->next_connection_id > 32767U) return ESP_ERR_NO_MEM;
    if (connection < domain->next_connection_id) return ESP_ERR_INVALID_STATE;
    twt_reserve(domain, lane, generation, ESP32_MQUICKJS_WIFI_TWT_INDIVIDUAL, options->timeout_ms, token);
    lane->requested.individual = options->config;
    lane->requested.individual.twt_id = (uint16_t)connection;
    domain->next_connection_id = connection + 1U;
    return ESP_OK;
}
esp_err_t esp32_mquickjs_wifi_twt_reserve_broadcast(esp32_mquickjs_wifi_twt_identity_t *domain,
    esp32_mquickjs_wifi_twt_lane_t *lane, uint32_t generation,
    const esp32_mquickjs_wifi_btwt_options_t *options, esp32_mquickjs_wifi_twt_token_t *token)
{
    if (!esp32_mquickjs_wifi_btwt_options_valid(options)) return ESP_ERR_INVALID_ARG;
    esp_err_t error = twt_admit(domain, lane, generation, token);
    if (error != ESP_OK) return error;
    twt_reserve(domain, lane, generation, ESP32_MQUICKJS_WIFI_TWT_BROADCAST, options->timeout_ms, token);
    lane->requested.broadcast = options->config;
    return ESP_OK;
}
static void twt_changed(esp32_mquickjs_wifi_twt_lane_t *lane)
{
    lane->sdk_retired = lane->event_fenced = false;
    lane->teardown_sdk_quiescent = lane->teardown_event_fenced = false;
    if (lane->revision == UINT32_MAX) lane->ambiguous = true;
    else ++lane->revision;
}
bool esp32_mquickjs_wifi_twt_begin_submit(esp32_mquickjs_wifi_twt_lane_t *lane,
    const esp32_mquickjs_wifi_twt_token_t *token)
{
    if (!twt_exact(lane, token) || lane->dispatching || lane->submitted ||
        lane->close_requested || lane->physical_termination) return false;
    twt_changed(lane);
    lane->dispatching = true;
    return true;
}
bool esp32_mquickjs_wifi_twt_submitted(esp32_mquickjs_wifi_twt_lane_t *lane,
    const esp32_mquickjs_wifi_twt_token_t *token, esp_err_t error,
    const esp32_mquickjs_wifi_twt_config_t *writeback)
{
    if (!twt_exact(lane, token) || !lane->dispatching || writeback == NULL) return false;
    lane->dispatching = false;
    lane->submitted = true;
    lane->submit_error = error;
    lane->dispatched = *writeback;
    if (lane->kind == ESP32_MQUICKJS_WIFI_TWT_INDIVIDUAL &&
        writeback->individual.twt_id != lane->requested.individual.twt_id) lane->ambiguous = true;
    twt_changed(lane);
    return true;
}
static bool twt_receiving(const esp32_mquickjs_wifi_twt_lane_t *lane, unsigned kind)
{
    return lane != NULL && lane->token.identity != 0U && lane->kind == kind &&
        (lane->dispatching || lane->submitted) && !lane->physical_termination;
}
/* Compare fields, not SDK struct padding, including the full 64-bit timestamp. */
static bool twt_same_individual(const wifi_event_sta_itwt_setup_t *a, const wifi_event_sta_itwt_setup_t *b)
{
#define SAME(field) (a->field == b->field)
    return SAME(status) && SAME(reason) && SAME(target_wake_time) &&
        SAME(config.setup_cmd) && SAME(config.trigger) && SAME(config.flow_type) &&
        SAME(config.flow_id) && SAME(config.wake_invl_expn) && SAME(config.wake_duration_unit) &&
        SAME(config.reserved) && SAME(config.min_wake_dura) && SAME(config.wake_invl_mant) &&
        SAME(config.twt_id) && SAME(config.timeout_time_ms);
#undef SAME
}
static bool twt_same_broadcast(const wifi_event_sta_btwt_setup_t *a, const wifi_event_sta_btwt_setup_t *b)
{
#define SAME(field) (a->field == b->field)
    return SAME(status) && SAME(setup_cmd) && SAME(btwt_id) && SAME(min_wake_dura) &&
        SAME(wake_invl_expn) && SAME(wake_invl_mant) && SAME(trigger) && SAME(flow_type) &&
        SAME(reason) && SAME(target_wake_time);
#undef SAME
}
bool esp32_mquickjs_wifi_twt_observe_individual(esp32_mquickjs_wifi_twt_lane_t *lane,
    const wifi_event_sta_itwt_setup_t *event)
{
    if (!twt_receiving(lane, ESP32_MQUICKJS_WIFI_TWT_INDIVIDUAL) || event == NULL ||
        event->config.twt_id != lane->requested.individual.twt_id) return false;
    twt_changed(lane);
    if (lane->setup_seen) {
        if (!twt_same_individual(&lane->setup.individual, event)) lane->ambiguous = true;
        return true;
    }
    lane->setup_seen = true;
    lane->setup.individual = *event;
    /* The SDK's success value is 1, NOT ESP_OK. Failure retains its exact code.
     * AP response commands/parameters are not subject to request-only limits. */
    lane->setup_success = event->status == 1;
    if (lane->setup_success) {
        lane->agreement_id = event->config.flow_id;
        if (event->config.setup_cmd != TWT_ACCEPT || event->config.reserved != 0U ||
            event->config.min_wake_dura == 0U || event->config.wake_invl_mant == 0U)
            lane->ambiguous = true;
    }
    return true;
}
bool esp32_mquickjs_wifi_twt_observe_broadcast(esp32_mquickjs_wifi_twt_lane_t *lane,
    const wifi_event_sta_btwt_setup_t *event)
{
    if (!twt_receiving(lane, ESP32_MQUICKJS_WIFI_TWT_BROADCAST) || event == NULL) return false;
    twt_changed(lane);
    if (lane->setup_seen) {
        if (!twt_same_broadcast(&lane->setup.broadcast, event)) lane->ambiguous = true;
        return true;
    }
    lane->setup_seen = true;
    lane->setup.broadcast = *event;
    lane->setup_success = event->status == BTWT_SETUP_SUCCESS;
    if ((unsigned)event->status > BTWT_SETUP_INTERNAL_ERR) lane->ambiguous = true;
    if (lane->setup_success) {
        lane->agreement_id = event->btwt_id;
        if (event->btwt_id == 0U || event->btwt_id > 31U || event->setup_cmd != TWT_ACCEPT ||
            event->min_wake_dura == 0U || event->wake_invl_mant == 0U ||
            event->wake_invl_expn > 31U || event->flow_type > 1U) lane->ambiguous = true;
    }
    return true;
}
bool esp32_mquickjs_wifi_twt_observe_teardown(esp32_mquickjs_wifi_twt_lane_t *lane,
    esp32_mquickjs_wifi_twt_kind_t kind, unsigned agreement_id, unsigned status)
{
    if (!twt_receiving(lane, kind) || !lane->setup_success || lane->agreement_id != agreement_id) return false;
    twt_changed(lane);
    if (status > 1U) { lane->ambiguous = true; return true; }
    if (lane->teardown_success && status == 0U) lane->ambiguous = true;
    lane->teardown_seen = true;
    lane->teardown_status = (int)status;
    if (status == 1U) lane->teardown_success = true;
    /* Retrying a failed transmission still requires independent native/event
     * drain. Success never becomes native retirement. */
    if (status == 0U) lane->teardown_written = false;
    return true;
}
bool esp32_mquickjs_wifi_twt_request_close(esp32_mquickjs_wifi_twt_lane_t *lane,
    const esp32_mquickjs_wifi_twt_token_t *token)
{
    if (!twt_exact(lane, token)) return false;
    lane->close_requested = true;
    return true;
}
bool esp32_mquickjs_wifi_twt_begin_teardown(esp32_mquickjs_wifi_twt_lane_t *lane,
    const esp32_mquickjs_wifi_twt_token_t *token)
{
    if (!twt_exact(lane, token) || !lane->submitted || !lane->close_requested || !lane->setup_success ||
        lane->dispatching || lane->teardown_busy || lane->teardown_success ||
        lane->ambiguous || lane->sdk_retired || lane->physical_termination) return false;
    if (lane->teardown_attempted && !(lane->teardown_sdk_quiescent && lane->teardown_event_fenced)) return false;
    twt_changed(lane);
    lane->teardown_busy = true;
    lane->teardown_attempted = true;
    lane->teardown_written = false;
    lane->teardown_seen = false;
    lane->teardown_status = -1;
    return true;
}
bool esp32_mquickjs_wifi_twt_teardown_submitted(esp32_mquickjs_wifi_twt_lane_t *lane,
    const esp32_mquickjs_wifi_twt_token_t *token, esp_err_t error)
{
    if (!twt_exact(lane, token) || !lane->teardown_busy) return false;
    lane->teardown_busy = false;
    lane->teardown_error = error;
    /* A callback may have run inside the SDK call. Its TX failure wins over
     * ESP_OK admission. Cleanup still needs native/event proof before retry. */
    lane->teardown_written = error == ESP_OK && !(lane->teardown_seen && lane->teardown_status == 0);
    twt_changed(lane);
    return true;
}
static bool twt_teardown_proof(const esp32_mquickjs_wifi_twt_lane_t *lane,
    const esp32_mquickjs_wifi_twt_token_t *token, uint32_t revision)
{
    return twt_exact(lane, token) && lane->teardown_attempted && !lane->dispatching &&
        !lane->teardown_busy && !lane->teardown_success && !lane->physical_termination &&
        !lane->sdk_retired && !lane->ambiguous && revision != UINT32_MAX && lane->revision == revision;
}
bool esp32_mquickjs_wifi_twt_teardown_quiescent(esp32_mquickjs_wifi_twt_lane_t *lane,
    const esp32_mquickjs_wifi_twt_token_t *token, uint32_t revision)
{
    if (!twt_teardown_proof(lane, token, revision)) return false;
    lane->teardown_sdk_quiescent = true;
    lane->teardown_event_fenced = false;
    return true;
}
bool esp32_mquickjs_wifi_twt_teardown_fenced(esp32_mquickjs_wifi_twt_lane_t *lane,
    const esp32_mquickjs_wifi_twt_token_t *token, uint32_t revision)
{
    if (!twt_teardown_proof(lane, token, revision) || !lane->teardown_sdk_quiescent) return false;
    lane->teardown_event_fenced = true;
    return true;
}
bool esp32_mquickjs_wifi_twt_retirement_revision(const esp32_mquickjs_wifi_twt_lane_t *lane,
    const esp32_mquickjs_wifi_twt_token_t *token, uint32_t *revision)
{
    if (!twt_exact(lane, token) || revision == NULL || !lane->submitted || lane->dispatching ||
        lane->teardown_busy || lane->physical_termination || lane->revision == UINT32_MAX) return false;
    if (!lane->close_requested && !lane->ambiguous && lane->submit_error == ESP_OK &&
        !lane->teardown_success && !(lane->setup_seen && !lane->setup_success)) return false;
    *revision = lane->revision;
    return true;
}
bool esp32_mquickjs_wifi_twt_sdk_retired(esp32_mquickjs_wifi_twt_lane_t *lane,
    const esp32_mquickjs_wifi_twt_token_t *token, uint32_t revision)
{
    uint32_t current;
    if (!esp32_mquickjs_wifi_twt_retirement_revision(lane, token, &current) || current != revision) return false;
    lane->sdk_retired = true;
    lane->event_fenced = false;
    return true;
}
bool esp32_mquickjs_wifi_twt_event_fenced(esp32_mquickjs_wifi_twt_lane_t *lane,
    const esp32_mquickjs_wifi_twt_token_t *token, uint32_t revision)
{
    uint32_t current;
    if (!esp32_mquickjs_wifi_twt_retirement_revision(lane, token, &current) || current != revision ||
        !lane->sdk_retired) return false;
    lane->event_fenced = true;
    return true;
}
bool esp32_mquickjs_wifi_twt_terminated(esp32_mquickjs_wifi_twt_lane_t *lane,
    const esp32_mquickjs_wifi_twt_token_t *token)
{
    if (!twt_exact(lane, token) || lane->dispatching || lane->teardown_busy) return false;
    lane->physical_termination = true;
    return true;
}
bool esp32_mquickjs_wifi_twt_release(esp32_mquickjs_wifi_twt_lane_t *lane,
    esp32_mquickjs_wifi_twt_token_t *token)
{
    if (!twt_exact(lane, token) || lane->dispatching || lane->teardown_busy) return false;
    if (lane->submitted && !lane->physical_termination && !(lane->sdk_retired && lane->event_fenced)) return false;
    memset(lane, 0, sizeof(*lane));
    *token = (esp32_mquickjs_wifi_twt_token_t){0};
    return true;
}
#endif
