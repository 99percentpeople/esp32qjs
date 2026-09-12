#pragma once
#include "sdkconfig.h"
#include <stdbool.h>
#include <stdint.h>
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI
#include "esp_err.h"

#define ESP32_MQUICKJS_WIFI_ACTION_EARLY_EVENTS 4U
typedef enum {
    ESP32_MQUICKJS_WIFI_ACTION_SEND = 1,
    ESP32_MQUICKJS_WIFI_ACTION_ROC = 2,
} esp32_mquickjs_wifi_action_kind_t;
typedef struct { uint32_t generation, identity; } esp32_mquickjs_wifi_action_token_t;
typedef struct { uint8_t operation_id, status; } esp32_mquickjs_wifi_action_early_t;
/* Internal native ledger, not a public type. Owner serializes every access,
 * including event delivery; no SDK/JS/allocation or callback-owned pointers.
 * Initialize next_identity=1 once per boot, never on runtime/driver restart.
 * Native channel/lease/security admission remains the Radio owner's duty. */
typedef struct {
    uint32_t next_identity, generation, identity, context, revision;
    esp_err_t submit_error, cancel_error;
    int8_t tx_status, terminal_status;
    uint8_t kind, interface, channel, operation_id, early_count;
    bool dispatching, submitted, cancel_requested, cancel_busy, cancel_written;
    bool ambiguous, terminal, sdk_fenced, event_fenced, physical_termination, sdk_quiescent;
    esp32_mquickjs_wifi_action_early_t early[ESP32_MQUICKJS_WIFI_ACTION_EARLY_EVENTS];
} esp32_mquickjs_wifi_action_lane_t;

esp_err_t esp32_mquickjs_wifi_action_reserve(esp32_mquickjs_wifi_action_lane_t *lane,
    uint32_t generation, esp32_mquickjs_wifi_action_kind_t kind, uint32_t context,
    uint8_t interface, uint8_t channel, esp32_mquickjs_wifi_action_token_t *token);
/* Call immediately before SDK submission. Abort without native proof is legal
 * only before this boundary. SDK errors do not imply no native operation. */
bool esp32_mquickjs_wifi_action_begin_submit(esp32_mquickjs_wifi_action_lane_t *lane,
    const esp32_mquickjs_wifi_action_token_t *token);
bool esp32_mquickjs_wifi_action_submitted(esp32_mquickjs_wifi_action_lane_t *lane,
    const esp32_mquickjs_wifi_action_token_t *token, esp_err_t error, uint8_t operation_id);
/* context is the SDK event's scalar rx_cb identity, not a dereferenceable pointer.
 * It must belong exclusively to this module. No JS observation queue is involved.
 * Returns true only when a matching event was recorded (possibly quarantined). */
bool esp32_mquickjs_wifi_action_observe(esp32_mquickjs_wifi_action_lane_t *lane,
    esp32_mquickjs_wifi_action_kind_t kind, uint32_t context, uint8_t interface,
    uint8_t channel, uint8_t operation_id, unsigned status);
bool esp32_mquickjs_wifi_action_request_cancel(esp32_mquickjs_wifi_action_lane_t *lane,
    const esp32_mquickjs_wifi_action_token_t *token);
bool esp32_mquickjs_wifi_action_begin_cancel(esp32_mquickjs_wifi_action_lane_t *lane,
    const esp32_mquickjs_wifi_action_token_t *token);
bool esp32_mquickjs_wifi_action_cancelled(esp32_mquickjs_wifi_action_lane_t *lane,
    const esp32_mquickjs_wifi_action_token_t *token, esp_err_t error);
/* Native completion precedes both fences. The caller must actually serialize a
 * driver call after terminal delivery (or a full SDK retirement probe), then a default event-loop marker carrying
 * this exact token+revision. Any later matching event invalidates both proofs. */
bool esp32_mquickjs_wifi_action_fence_revision(const esp32_mquickjs_wifi_action_lane_t *lane,
    const esp32_mquickjs_wifi_action_token_t *token, uint32_t *revision);
bool esp32_mquickjs_wifi_action_sdk_fenced(esp32_mquickjs_wifi_action_lane_t *lane,
    const esp32_mquickjs_wifi_action_token_t *token, uint32_t revision);
bool esp32_mquickjs_wifi_action_event_fenced(esp32_mquickjs_wifi_action_lane_t *lane,
    const esp32_mquickjs_wifi_action_token_t *token, uint32_t revision);
/* SDK queue probe proved its entire off-channel record is retired. No guessed
 * terminal status is inserted. Still requires an exact event-loop fence before
 * release; every subsequent matching event invalidates this proof. */
bool esp32_mquickjs_wifi_action_quiescent(esp32_mquickjs_wifi_action_lane_t *lane,
    const esp32_mquickjs_wifi_action_token_t *token, uint32_t revision);
/* Only the Radio cleanup owner may attest actual deinit and default event-loop drain.
 * Allows recovery from missing/ambiguous native completion, never from timeout. */
bool esp32_mquickjs_wifi_action_terminated(esp32_mquickjs_wifi_action_lane_t *lane,
    const esp32_mquickjs_wifi_action_token_t *token);
bool esp32_mquickjs_wifi_action_release(esp32_mquickjs_wifi_action_lane_t *lane,
    esp32_mquickjs_wifi_action_token_t *token);
#endif
