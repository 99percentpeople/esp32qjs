#pragma once
#include "sdkconfig.h"
#if CONFIG_ESP32_MQUICKJS_WIFI_RADIO
#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint32_t generation, owner_identity, identity;
} esp32_mquickjs_wifi_interval_token_t;
typedef struct {
    esp32_mquickjs_wifi_interval_token_t owner;
    uint32_t generation, revision;
    uint16_t value, previous;
    esp_err_t error, restore_error; /* Last policy write and restoration, separately. */
    bool known, uncertain, restore_pending;
} esp32_mquickjs_wifi_interval_state_t;
typedef struct {
    uint32_t revision;
    esp_err_t error;
    bool attempted, accepted;
} esp32_mquickjs_wifi_interval_result_t;
typedef struct {
    uint32_t generation, revision;
    uint16_t value;
} esp32_mquickjs_wifi_interval_snapshot_t;
typedef esp_err_t (*esp32_mquickjs_wifi_interval_writer_t)(void *opaque, uint16_t milliseconds);

/* Serialized by the caller's Radio mutation mutex. Caller proves driver state,
 * exact Radio owner and exclusion of conflicting global users. SDK writes run
 * outside IRQ critical sections. No allocations, retained pointers or defaults.
 * All input/output storage is stable and distinct from state.
 * Zero is the explicit SDK default-mode request, not an inferred prior value.
 * Generation/Radio owner identities are supplied by the real Radio registry.
 * Revision never wraps or resets across physical deinitialization. */
esp_err_t esp32_mquickjs_wifi_interval_write(esp32_mquickjs_wifi_interval_state_t *state,
    uint32_t generation, uint16_t milliseconds, esp32_mquickjs_wifi_interval_writer_t writer,
    void *opaque, esp32_mquickjs_wifi_interval_result_t *result);
/* Requires a known accepted value in the same generation. Token is returned even
 * if the SDK write fails: caller then owns a restoration obligation. Reserve at
 * least one write revision for restore; repeated restore failures may exhaust it. */
esp_err_t esp32_mquickjs_wifi_interval_acquire(esp32_mquickjs_wifi_interval_state_t *state,
    uint32_t generation, uint32_t owner_identity, uint16_t milliseconds,
    esp32_mquickjs_wifi_interval_writer_t writer, void *opaque,
    esp32_mquickjs_wifi_interval_token_t *token, esp32_mquickjs_wifi_interval_result_t *result);
esp_err_t esp32_mquickjs_wifi_interval_update(esp32_mquickjs_wifi_interval_state_t *state,
    const esp32_mquickjs_wifi_interval_token_t *token, uint16_t milliseconds,
    esp32_mquickjs_wifi_interval_writer_t writer, void *opaque,
    esp32_mquickjs_wifi_interval_result_t *result);
/* Retry only the retained restoration value. Success clears the owner's token;
 * failure retains both state and token. No SDK getter/readback is claimed. */
esp_err_t esp32_mquickjs_wifi_interval_release(esp32_mquickjs_wifi_interval_state_t *state,
    esp32_mquickjs_wifi_interval_token_t *token, esp32_mquickjs_wifi_interval_writer_t writer,
    void *opaque, esp32_mquickjs_wifi_interval_result_t *result);
/* Freeze only same-generation accepted, unborrowed intent. Reserve revision
 * capacity for the new driver's baseline and one replay, not infinite retries.
 * Failure clears output; no SDK observation or invented default is involved. */
esp_err_t esp32_mquickjs_wifi_interval_capture(const esp32_mquickjs_wifi_interval_state_t *state,
    uint32_t generation, esp32_mquickjs_wifi_interval_snapshot_t *snapshot);
/* Caller proves actual deinit/init and owns the frozen lifecycle. The new
 * driver must have an accepted baseline. Failure quarantines normal knowledge;
 * retain the original snapshot for a subsequent physical-generation retry. */
esp_err_t esp32_mquickjs_wifi_interval_replay(esp32_mquickjs_wifi_interval_state_t *state,
    uint32_t generation, const esp32_mquickjs_wifi_interval_snapshot_t *snapshot,
    esp32_mquickjs_wifi_interval_writer_t writer, void *opaque,
    esp32_mquickjs_wifi_interval_result_t *result);
/* After successful physical deinit or an admitted default-reset attempt, with all temporary owners retired.
 * A generation change alone, runtime restart, timeout or SDK error is no proof. */
bool esp32_mquickjs_wifi_interval_invalidate(esp32_mquickjs_wifi_interval_state_t *state,
    uint32_t generation);
#endif
