#pragma once
#include "sdkconfig.h"
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI
#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

typedef enum {
    ESP32_MQUICKJS_WIFI_POLICY_SLOT_DYNAMIC_CS,
    ESP32_MQUICKJS_WIFI_POLICY_SLOT_STA_11B,
    ESP32_MQUICKJS_WIFI_POLICY_SLOT_AP_11B,
    ESP32_MQUICKJS_WIFI_POLICY_SLOT_COEX_POWER,
#if CONFIG_SOC_WIFI_HE_SUPPORT
    ESP32_MQUICKJS_WIFI_POLICY_SLOT_BSS_COLOR,
#endif
    ESP32_MQUICKJS_WIFI_POLICY_SLOT_COUNT,
} esp32_mquickjs_wifi_policy_slot_t;
typedef struct {
    uint32_t generation, revision, accepted_revision;
    esp_err_t error;
    bool requested, value, configured, known, uncertain;
} esp32_mquickjs_wifi_policy_record_t;
typedef struct {
    esp32_mquickjs_wifi_policy_record_t records[ESP32_MQUICKJS_WIFI_POLICY_SLOT_COUNT];
    uint32_t revision;
} esp32_mquickjs_wifi_policy_state_t;
typedef struct {
    uint32_t revision;
    esp_err_t error;
    bool attempted, accepted;
} esp32_mquickjs_wifi_policy_write_t;
typedef esp_err_t (*esp32_mquickjs_wifi_policy_writer_t)(void *opaque,
    esp32_mquickjs_wifi_policy_slot_t slot, bool requested);

/* Radio mutation mutex and driver/owner/feature admission are the caller's duty.
 * No allocation, pointers retained, inferred default, deduplication or rollback.
 * Before calling SDK, publish a unique revision and unknown/uncertain state.
 * A failure preserves the last accepted value as history, never current truth.
 * Generation cannot change while any record still describes a live driver.
 * Input/output storage is stable and distinct from state. */
esp_err_t esp32_mquickjs_wifi_policy_apply(esp32_mquickjs_wifi_policy_state_t *state,
    uint32_t generation, esp32_mquickjs_wifi_policy_slot_t slot, bool requested,
    esp32_mquickjs_wifi_policy_writer_t writer, void *opaque,
    esp32_mquickjs_wifi_policy_write_t *result);
/* After successful physical deinit, or an admitted default-reset attempt with no owners. Keep revision space and accepted
 * history; clear live knowledge. History alone does not authorize replay. */
void esp32_mquickjs_wifi_policy_invalidate(esp32_mquickjs_wifi_policy_state_t *state);
/* Frozen restart intent, copied before physical deinit. Only current accepted
 * records qualify; stale accepted history and uncertain attempts are rejected. */
typedef struct {
    uint32_t generation, revision;
    uint8_t mask, values;
} esp32_mquickjs_wifi_policy_snapshot_t;
esp_err_t esp32_mquickjs_wifi_policy_capture(const esp32_mquickjs_wifi_policy_state_t *state,
    uint32_t generation, esp32_mquickjs_wifi_policy_snapshot_t *snapshot);
/* Caller proves exact lifecycle ownership and the real pre-/post-start phase.
 * Nonempty replay requires a new physical generation. Completed bits advance only after SDK
 * acceptance; retry skips successful prefix. Every write uses the normal ledger.
 * Post-start dynamic CS cannot run before all pre-start policies completed. */
esp_err_t esp32_mquickjs_wifi_policy_replay(esp32_mquickjs_wifi_policy_state_t *state,
    uint32_t generation, const esp32_mquickjs_wifi_policy_snapshot_t *snapshot,
    bool after_start, uint8_t *completed, esp32_mquickjs_wifi_policy_writer_t writer,
    void *opaque, esp32_mquickjs_wifi_policy_write_t *result);
#endif
