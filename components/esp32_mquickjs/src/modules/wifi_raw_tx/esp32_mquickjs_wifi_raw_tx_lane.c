#include "esp32_mquickjs_wifi_raw_tx_lane.h"
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI
#include "freertos/FreeRTOS.h"
#include <stddef.h>

#define NO_ACTIVE UINT8_MAX
static portMUX_TYPE s_lane_lock = portMUX_INITIALIZER_UNLOCKED;
static struct {
    uint32_t requests[ESP32_MQUICKJS_WIFI_RAW_TX_LANE_CAPACITY];
    uint32_t next_identity;
    uint8_t active_index;
} s_lane = {.next_identity = 1, .active_index = NO_ACTIVE};

/* Caller holds s_lane_lock. Token is caller-owned, never an SDK cookie. */
static bool lane_exact(const esp32_mquickjs_wifi_raw_tx_lane_token_t *token)
{
    return token != NULL && token->identity != 0U &&
        token->index < ESP32_MQUICKJS_WIFI_RAW_TX_LANE_CAPACITY &&
        s_lane.requests[token->index] == token->identity;
}

esp32_mquickjs_wifi_raw_tx_lane_result_t esp32_mquickjs_wifi_raw_tx_lane_request(
    esp32_mquickjs_wifi_raw_tx_lane_token_t *token)
{
    if (token == NULL || token->identity != 0U || token->index != 0U)
        return ESP32_MQUICKJS_WIFI_RAW_TX_LANE_INVALID;
    portENTER_CRITICAL(&s_lane_lock);
    esp32_mquickjs_wifi_raw_tx_lane_result_t result = ESP32_MQUICKJS_WIFI_RAW_TX_LANE_FULL;
    if (s_lane.next_identity == 0U) result = ESP32_MQUICKJS_WIFI_RAW_TX_LANE_EXHAUSTED;
    else for (uint8_t i = 0; i < ESP32_MQUICKJS_WIFI_RAW_TX_LANE_CAPACITY; ++i) {
        if (s_lane.requests[i] != 0U) continue;
        uint32_t identity = s_lane.next_identity;
        s_lane.next_identity = identity == UINT32_MAX ? 0U : identity + 1U;
        s_lane.requests[i] = identity;
        *token = (esp32_mquickjs_wifi_raw_tx_lane_token_t){.identity = identity, .index = i};
        result = ESP32_MQUICKJS_WIFI_RAW_TX_LANE_OK;
        break;
    }
    portEXIT_CRITICAL(&s_lane_lock);
    return result;
}

bool esp32_mquickjs_wifi_raw_tx_lane_acquire(const esp32_mquickjs_wifi_raw_tx_lane_token_t *token)
{
    portENTER_CRITICAL(&s_lane_lock);
    bool ok = lane_exact(token);
    if (ok && s_lane.active_index != NO_ACTIVE) ok = s_lane.active_index == token->index;
    else if (ok) {
        for (uint8_t i = 0; i < ESP32_MQUICKJS_WIFI_RAW_TX_LANE_CAPACITY; ++i) {
            if (s_lane.requests[i] != 0U && s_lane.requests[i] < token->identity) { ok = false; break; }
        }
        if (ok) s_lane.active_index = token->index;
    }
    portEXIT_CRITICAL(&s_lane_lock);
    return ok;
}

static bool lane_remove(esp32_mquickjs_wifi_raw_tx_lane_token_t *token, bool active)
{
    portENTER_CRITICAL(&s_lane_lock);
    bool ok = lane_exact(token) && ((s_lane.active_index == token->index) == active);
    if (ok) {
        s_lane.requests[token->index] = 0;
        if (active) s_lane.active_index = NO_ACTIVE;
        *token = (esp32_mquickjs_wifi_raw_tx_lane_token_t){0};
    }
    portEXIT_CRITICAL(&s_lane_lock);
    return ok;
}

bool esp32_mquickjs_wifi_raw_tx_lane_withdraw(esp32_mquickjs_wifi_raw_tx_lane_token_t *token)
{
    return lane_remove(token, false);
}

bool esp32_mquickjs_wifi_raw_tx_lane_release(esp32_mquickjs_wifi_raw_tx_lane_token_t *token)
{
    return lane_remove(token, true);
}

void esp32_mquickjs_wifi_raw_tx_lane_status(esp32_mquickjs_wifi_raw_tx_lane_status_t *output)
{
    if (output == NULL) return;
    esp32_mquickjs_wifi_raw_tx_lane_status_t status = {0};
    portENTER_CRITICAL(&s_lane_lock);
    status.identity_exhausted = s_lane.next_identity == 0U;
    for (uint8_t i = 0; i < ESP32_MQUICKJS_WIFI_RAW_TX_LANE_CAPACITY; ++i) {
        if (s_lane.requests[i] == 0U) continue;
        if (s_lane.active_index == i) status.active_identity = s_lane.requests[i];
        else ++status.waiting;
    }
    portEXIT_CRITICAL(&s_lane_lock);
    *output = status;
}
#endif
