#pragma once
#include "esp32_mquickjs_wifi_raw_tx_snapshot.h"
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI
#include "esp_err.h"

typedef struct { uint32_t generation, identity, radio_lease_identity; } esp32_mquickjs_wifi_raw_tx_token_t;
typedef struct {
    bool registered, registration_uncertain, unregister_written, control_busy;
    bool operation_active, submit_returned, driver_accepted, driver_completed;
    /* Successful physical deinit, not TX completion. Retain exact token and
     * observations until its owner retires; a new registration is excluded. */
    bool native_terminated;
    bool abandoned, quarantined, correlation_fault, identity_exhausted;
    uint32_t generation, callbacks_active;
    uint32_t orphan_callbacks, invalid_callbacks, mismatched_callbacks, duplicate_callbacks;
    esp32_mquickjs_wifi_raw_tx_token_t token;
    esp_err_t submit_error, cleanup_error;
    uint64_t submitted_at_us;
    uint16_t byte_length;
    esp32_mquickjs_wifi_raw_tx_frame_type_t frame_type;
    esp32_mquickjs_wifi_raw_tx_snapshot_t completion;
} esp32_mquickjs_wifi_raw_tx_broker_status_t;

/* Boot-owned singleton: only the Radio mutation owner may register/submit/retire/
 * unregister/reset. The caller holds an exact live Radio lease until retire.
 * These operations call SDK/allocator outside short snapshot critical sections.
 * Only abandon/status and the SDK callback are usable from other tasks. There
 * is no callback context pointing at JS, runtime tasks or a Session allocation. */
esp_err_t esp32_mquickjs_wifi_raw_tx_broker_register(uint32_t generation);
esp_err_t esp32_mquickjs_wifi_raw_tx_broker_submit(uint32_t generation, uint32_t radio_lease_identity,
    const uint8_t *bytes, size_t length, const esp32_mquickjs_wifi_raw_tx_validation_policy_t *policy,
    esp32_mquickjs_wifi_raw_tx_token_t *token, esp32_mquickjs_wifi_raw_tx_validation_t *validation);
bool esp32_mquickjs_wifi_raw_tx_broker_abandon(const esp32_mquickjs_wifi_raw_tx_token_t *token);
bool esp32_mquickjs_wifi_raw_tx_broker_retire(esp32_mquickjs_wifi_raw_tx_token_t *token);
void esp32_mquickjs_wifi_raw_tx_broker_status(esp32_mquickjs_wifi_raw_tx_broker_status_t *output);
/* Seals registration until physical deinit. Success preserves generation and
 * unregister_written; repeating it only retries callback drain, never the SDK
 * unregister mutation. This is shutdown cleanup, not a per-packet operation. */
esp_err_t esp32_mquickjs_wifi_raw_tx_broker_unregister(uint32_t generation);
/* Recovery-only variant: Radio has reserved exclusive physical teardown for
 * this exact outstanding token. Seals callback registration without retiring
 * payload/token. Success is NOT native termination; physical deinit is required. */
esp_err_t esp32_mquickjs_wifi_raw_tx_broker_quiesce(uint32_t generation,
    const esp32_mquickjs_wifi_raw_tx_token_t *token);
/* Sole escape from unresolved native ownership/correlation faults. Radio must
 * have successfully deinitialized this physical generation AND drained callback
 * execution. Unregister, timeout, JS GC and runtime restart are not this proof.
 * This function performs no driver reset and must never be called by public JS.
 * It releases the driver copy but preserves an outstanding token/observations as
 * native_terminated. Exact retire must consume that proof before register/submit
 * can reuse the broker. Idempotent until the termination record is consumed. */
bool esp32_mquickjs_wifi_raw_tx_broker_reset_after_deinit(uint32_t generation);
#endif
