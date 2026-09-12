#pragma once
#include "sdkconfig.h"
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI && CONFIG_SOC_WIFI_HE_SUPPORT && CONFIG_IDF_TARGET_ESP32C5
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

typedef enum {
    ESP32_MQUICKJS_WIFI_TWT_TX_OK = 0,
    ESP32_MQUICKJS_WIFI_TWT_TX_NO_MEMORY,
    ESP32_MQUICKJS_WIFI_TWT_TX_CAPACITY,
    ESP32_MQUICKJS_WIFI_TWT_TX_DUPLICATE,
    ESP32_MQUICKJS_WIFI_TWT_TX_REVISION_EXHAUSTED,
    ESP32_MQUICKJS_WIFI_TWT_TX_PROBE_SCOPE,
    ESP32_MQUICKJS_WIFI_TWT_TX_SETUP_IDENTITY,
    ESP32_MQUICKJS_WIFI_TWT_TX_INFORMATION_IDENTITY,
    ESP32_MQUICKJS_WIFI_TWT_TX_BROADCAST_IDENTITY,
} esp32_mquickjs_wifi_twt_tx_fault_t;
typedef struct {
    uint32_t revision, reserved_bytes;
    uint16_t tracked, output_calls, recycle_calls, probe_calls;
    esp32_mquickjs_wifi_twt_tx_fault_t fault;
    bool probe_buffer_present; /* Includes output/callback/recycler pins. */
    uint32_t broadcast_dialog_exhausted_mask; /* Per ID, sticky until device reboot. */
} esp32_mquickjs_wifi_twt_tx_snapshot_t;

/* Boot-scoped observation of setup/teardown/information and TWT probe buffers.
 * The bounded internal-memory ledger is allocated on first use and retained
 * across runtime/driver restart. Faults and revision exhaustion are sticky.
 * Probe records additionally pin early callback status until SDK submission
 * returns; the patched native callback only forwards the exact live EB once.
 * Individual setup records bind a non-reused response timer identity before
 * output; late callbacks cannot act on a replacement pending slot or flow.
 * Information records bind the associated node and exact flow/request IDs.
 * Information and broadcast setup share an optional union array across the
 * 64 TX slots plus 32 boot-scoped dialog counters (1568 bytes on C5).
 * Broadcast IDs each have 255 nonzero wire dialogs; no counter resets on
 * runtime/driver restart or connection close. Exhaustion returns NO_MEM before
 * output and does not fault unrelated IDs. Broadcast setup stores node/parameter/dialog
 * values and is revoked by native connection close before timer retirement.
 * PM remains owned until callback or Wi-Fi-task cleanup after native recycle.
 * No-cookie completion relies on native callback-before-recycle ordering;
 * a fabricated callback after full native retirement is not identifiable.
 * No pointers escape. Zero tracked is NOT complete TWT retirement: an active
 * probe call can still arm its response timer; timer rearming, native work
 * and default-event-loop drainage remain separate.
 * No public caller can mark a buffer complete or reset this ledger. */
void esp32_mquickjs_wifi_twt_tx_snapshot(esp32_mquickjs_wifi_twt_tx_snapshot_t *output);
/* Wi-Fi native task only: repay information PM references after both output
 * and native recycler returned. Does not prove timer/event/RF retirement. */
void esp32_mquickjs_wifi_twt_tx_cleanup_native(void);
/* Native task only: block overlapping broadcast IDs before the SDK writes
 * shared timeout state; revoke all old callbacks at connection PM teardown. */
/* Read-only remaining wire identities for this boot; no implicit allocation. */
uint16_t esp32_mquickjs_wifi_twt_tx_broadcast_remaining(unsigned slot);
esp_err_t esp32_mquickjs_wifi_twt_tx_broadcast_admit_native(unsigned broadcast_id);
void esp32_mquickjs_wifi_twt_tx_broadcast_cancel_native(void);
/* Native task, exact broadcast setup request. Cancellation revokes callback
 * permission but keeps its EB pinned through output/callback/recycle. Quiet
 * returns a revision only when that TX is absent; not a timer/event fence. */
esp_err_t esp32_mquickjs_wifi_twt_tx_broadcast_cancel_request_native(unsigned slot, uint32_t identity);
esp_err_t esp32_mquickjs_wifi_twt_tx_broadcast_quiescent_native(unsigned slot, uint32_t identity, uint32_t *revision);
/* Exact information TX revision, zero checks all information TX buffers. */
esp_err_t esp32_mquickjs_wifi_twt_tx_information_quiescent_native(uint32_t identity);
/* Value-only lookup in the current live TX ledger. Known unmanaged buffers
 * return identity zero; an address retained by a retired operation cannot
 * classify a newly allocated buffer at that same address. */
/* Bounded three-byte broadcast teardown parser, output and completion layouts.
 * Rejects all-ID teardown; that needs a separate explicit multi-owner policy. */
bool esp32_mquickjs_wifi_twt_tx_broadcast_teardown_fields(void *buffer, bool completion, uint8_t *slot);
bool esp32_mquickjs_wifi_twt_tx_teardown_identity(void *buffer, uint32_t *identity);
#endif
