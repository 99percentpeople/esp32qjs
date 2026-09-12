#pragma once
#include "sdkconfig.h"
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI
#include "esp_wifi.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    const char *name;
    wifi_phy_rate_t rate;
    uint8_t family; /* internal mapping only */
    uint8_t mcs;
} esp32_mquickjs_wifi_tx_rate_entry_t;
/* SDK enum constants, never ordinal arithmetic across target-specific MCS enums. */
const esp32_mquickjs_wifi_tx_rate_entry_t *esp32_mquickjs_wifi_tx_rate_entries(size_t *count);
const char *esp32_mquickjs_wifi_tx_rate_name(int32_t rate);
const char *esp32_mquickjs_wifi_tx_phy_name(wifi_phy_mode_t mode);
bool esp32_mquickjs_wifi_tx_rate_valid(const wifi_tx_rate_config_t *config);

typedef struct {
    bool known, uncertain;
    uint32_t generation, write_identity;
    wifi_tx_rate_config_t config;
    esp_err_t error, rollback_error;
} esp32_mquickjs_wifi_tx_rate_record_t;
typedef struct {
    esp32_mquickjs_wifi_tx_rate_record_t records[2]; /* station, AP */
    uint32_t next_identity;
} esp32_mquickjs_wifi_tx_rate_state_t;
typedef struct {
    uint32_t identity;
    bool attempted, accepted, rollback_attempted, restored, uncertain;
    esp_err_t error, rollback_error;
} esp32_mquickjs_wifi_tx_rate_write_t;
/* One exclusive temporary interface rate. Owned by the Radio mutation mutex;
 * retains only values, never caller/Session/JS pointers. identity=0 is empty. */
typedef struct {
    uint32_t generation, identity, write_identity;
    wifi_interface_t interface;
    wifi_tx_rate_config_t previous;
    bool restore_pending;
    esp_err_t restore_error;
} esp32_mquickjs_wifi_tx_rate_lease_t;
typedef struct {
    wifi_tx_rate_config_t configs[2];
    uint32_t generation;
    uint8_t mask;
} esp32_mquickjs_wifi_tx_rate_snapshot_t;
typedef esp_err_t (*esp32_mquickjs_wifi_tx_rate_writer_t)(void *opaque,
    wifi_interface_t interface, const wifi_tx_rate_config_t *config);

/* Caller holds the Radio mutation mutex and has proved initialized/stopped,
 * owner-free driver admission or the sole exact temporary-rate owner. No allocation/JS/implicit init/restart. Writer
 * is the injectable production SDK boundary; it must not retain config pointer.
 * State starts with next_identity=1; identities never wrap or reset on deinit.
 * Failure after a write is uncertain unless a known previous value was reapplied
 * successfully. A failed call never fabricates the driver default or a readback.
 * Inputs/outputs are distinct stable caller storage, not aliases of state. */
esp_err_t esp32_mquickjs_wifi_tx_rate_apply(esp32_mquickjs_wifi_tx_rate_state_t *state,
    uint32_t generation, wifi_interface_t interface, const wifi_tx_rate_config_t *config,
    esp32_mquickjs_wifi_tx_rate_writer_t writer, void *opaque,
    esp32_mquickjs_wifi_tx_rate_write_t *output);
/* Shared STA/AP pre-start transaction. Admission is read-only and precedes
 * Radio owner allocation. The caller separately proves stopped driver, exclusive
 * ownership and the appropriate interface/helper lifecycle; this helper grants
 * no authority to start/stop an AP or disconnect clients. */
esp_err_t esp32_mquickjs_wifi_tx_rate_borrow_admission(const esp32_mquickjs_wifi_tx_rate_state_t *state,
    uint32_t generation, wifi_interface_t interface, const wifi_tx_rate_config_t *config,
    const esp32_mquickjs_wifi_tx_rate_lease_t *lease);
/* Revalidates admission, then publishes the predecessor before calling writer.
 * Failed write + failed rollback retains the exact lease for cleanup. */
esp_err_t esp32_mquickjs_wifi_tx_rate_borrow(esp32_mquickjs_wifi_tx_rate_state_t *state,
    uint32_t generation, uint32_t owner_identity, wifi_interface_t interface,
    const wifi_tx_rate_config_t *config, esp32_mquickjs_wifi_tx_rate_lease_t *lease,
    esp32_mquickjs_wifi_tx_rate_writer_t writer, void *opaque);
/* Caller proves STOP/event drain first. Restores only the selected owner and
 * latest write identity; a failed suffix retains predecessor and obligation.
 * This does not release a Radio lease or clear unrelated lifecycle faults. */
esp_err_t esp32_mquickjs_wifi_tx_rate_restore(esp32_mquickjs_wifi_tx_rate_state_t *state,
    uint32_t generation, uint32_t owner_identity, esp32_mquickjs_wifi_tx_rate_lease_t *lease,
    esp32_mquickjs_wifi_tx_rate_writer_t writer, void *opaque);
/* Freeze only current accepted records. Never-configured slots remain absent;
 * stale/uncertain records fail instead of inventing SDK defaults. Caller first
 * retires temporary rate leases and keeps the snapshot under one lifecycle. */
esp_err_t esp32_mquickjs_wifi_tx_rate_restart_capture(const esp32_mquickjs_wifi_tx_rate_state_t *state,
    uint32_t generation, esp32_mquickjs_wifi_tx_rate_snapshot_t *snapshot);
/* Exact Raw TX physical recovery may retain a temporary lease. Freeze its
 * validated predecessor without changing the live ledger, then use normal
 * replay in the new generation. Non-borrowed records are captured unchanged. */
esp_err_t esp32_mquickjs_wifi_tx_rate_recovery_capture(const esp32_mquickjs_wifi_tx_rate_state_t *state,
    uint32_t generation, uint32_t owner_identity, const esp32_mquickjs_wifi_tx_rate_lease_t *temporary,
    esp32_mquickjs_wifi_tx_rate_snapshot_t *snapshot);
bool esp32_mquickjs_wifi_tx_rate_replay_capacity(const esp32_mquickjs_wifi_tx_rate_state_t *state,
    const esp32_mquickjs_wifi_tx_rate_snapshot_t *snapshot);
/* Caller proves a new physical generation, stopped driver and same lifecycle.
 * completed belongs to that generation, advances only on success and is reset
 * after each physical rebuild. Frozen configs are never replaced after error. */
esp_err_t esp32_mquickjs_wifi_tx_rate_replay(esp32_mquickjs_wifi_tx_rate_state_t *state,
    uint32_t generation, const esp32_mquickjs_wifi_tx_rate_snapshot_t *snapshot, uint8_t *completed,
    esp32_mquickjs_wifi_tx_rate_writer_t writer, void *opaque, esp32_mquickjs_wifi_tx_rate_write_t *output);
/* Called after successful physical deinit or an admitted owner-free default-reset attempt. Clears current knowledge, not
 * identity space. Restart reapplication remains the lifecycle adapter's job. */
void esp32_mquickjs_wifi_tx_rate_invalidate(esp32_mquickjs_wifi_tx_rate_state_t *state);
bool esp32_mquickjs_wifi_tx_rate_uncertain(const esp32_mquickjs_wifi_tx_rate_state_t *state);
#endif
