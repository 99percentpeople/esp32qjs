#pragma once
#include "esp32_mquickjs_wifi_raw_tx_limits.h"
#include "sdkconfig.h"
#include <stdbool.h>
#include <stdint.h>

#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI
/* Eight Session requests plus the one-shot Future lane. A Session may hold at
 * most one request, independently of its packet queue capacity. */
typedef struct { uint32_t identity; uint8_t index; } esp32_mquickjs_wifi_raw_tx_lane_token_t;
typedef enum {
    ESP32_MQUICKJS_WIFI_RAW_TX_LANE_OK,
    ESP32_MQUICKJS_WIFI_RAW_TX_LANE_FULL,
    ESP32_MQUICKJS_WIFI_RAW_TX_LANE_EXHAUSTED,
    ESP32_MQUICKJS_WIFI_RAW_TX_LANE_INVALID,
} esp32_mquickjs_wifi_raw_tx_lane_result_t;
typedef struct {
    uint32_t active_identity;
    uint8_t waiting;
    bool identity_exhausted;
} esp32_mquickjs_wifi_raw_tx_lane_status_t;

/* Boot-owned FIFO arbiter, separate from Radio leases and callback identities.
 * No SDK calls, allocation, JS/runtime pointers or packet storage. The caller
 * owns its token storage and serializes access to that storage. All APIs are
 * task-context only; internal critical sections scan at most CAPACITY entries.
 * Request requires an empty token. Success admits a waiter, not an RF packet.
 * Only the oldest live request may acquire; repeated acquire is idempotent.
 * Identities never reset on Radio/runtime restart and never wrap. */
esp32_mquickjs_wifi_raw_tx_lane_result_t esp32_mquickjs_wifi_raw_tx_lane_request(
    esp32_mquickjs_wifi_raw_tx_lane_token_t *token);
bool esp32_mquickjs_wifi_raw_tx_lane_acquire(const esp32_mquickjs_wifi_raw_tx_lane_token_t *token);
/* Withdraw only a waiter. It cannot release the active grant. */
bool esp32_mquickjs_wifi_raw_tx_lane_withdraw(esp32_mquickjs_wifi_raw_tx_lane_token_t *token);
/* Release only an active grant, AFTER native retirement and required cleanup.
 * This is an ownership handoff, not native-termination proof. A public Future
 * timeout must transfer the grant to a retained native cleanup owner. Exact
 * stale/duplicate tokens cannot release another operation. Success zeroes token. */
bool esp32_mquickjs_wifi_raw_tx_lane_release(esp32_mquickjs_wifi_raw_tx_lane_token_t *token);
void esp32_mquickjs_wifi_raw_tx_lane_status(esp32_mquickjs_wifi_raw_tx_lane_status_t *output);
#endif
