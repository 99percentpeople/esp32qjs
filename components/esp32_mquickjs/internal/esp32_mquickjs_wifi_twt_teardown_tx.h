#pragma once
#include "sdkconfig.h"
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI && CONFIG_SOC_WIFI_HE_SUPPORT && CONFIG_IDF_TARGET_ESP32C5
#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>
typedef struct {
    uint32_t identity, revision;
    esp_err_t fault, submit_error;
    esp_err_t completion_error, observation_error;
    uint8_t flow, completion_status;
    bool broadcast, completion_seen, completion_ambiguous;
    bool submitting, wake_held, acquiring, releasing;
    bool output_seen, output_returned, recycling, recycled, callback_seen, callback_busy;
} esp32_mquickjs_wifi_twt_teardown_tx_snapshot_t;
/* One native teardown submission/PM authority at a time. Caller holds Radio
 * admission through full retirement. No SDK/JS-owned argument is retained.
 * Begin precedes result attempted; abandon is only legal before any driver
 * wake/output. End acknowledges native submission, never TX completion. */
esp_err_t esp32_mquickjs_wifi_twt_teardown_tx_begin_native(uint32_t identity, uintptr_t node, uint8_t flow);
/* Broadcast uses the same singleton/TX/recycler/PM ledger. The result remains
 * pinned until the caller supplies full retirement; begin never replaces it. */
esp_err_t esp32_mquickjs_wifi_twt_teardown_tx_begin_broadcast_native(uint32_t identity, uintptr_t node, uint8_t slot);
bool esp32_mquickjs_wifi_twt_teardown_tx_abandon_native(uint32_t identity);
esp_err_t esp32_mquickjs_wifi_twt_teardown_tx_end_native(uint32_t identity, esp_err_t error);
esp_err_t esp32_mquickjs_wifi_twt_teardown_tx_output_native(void *node, void *buffer, bool tracked, uint32_t *identity);
void esp32_mquickjs_wifi_twt_teardown_tx_output_returned(void *buffer, uint32_t identity);
/* Existing common recycler calls this only for tracked TWT buffers. Both
 * phases are IRAM-safe and manipulate internal state only, never driver PM. */
void esp32_mquickjs_wifi_twt_teardown_tx_recycle(void *buffer, uint32_t identity, bool entering);
/* Native queue only: settle a retained wake reference after proven recycle.
 * A quiet cut still requires external timer/native/event ordering. Release
 * consumes that proof and the exact unchanged revision, not an elapsed time. */
esp_err_t esp32_mquickjs_wifi_twt_teardown_tx_quiescent_native(uint32_t identity, uint32_t *revision);
bool esp32_mquickjs_wifi_twt_teardown_tx_release_native(uint32_t identity, uint32_t revision);
bool esp32_mquickjs_wifi_twt_teardown_tx_holds_broadcast(unsigned slot, uint32_t identity);
void esp32_mquickjs_wifi_twt_teardown_tx_snapshot(esp32_mquickjs_wifi_twt_teardown_tx_snapshot_t *out);
bool esp32_mquickjs_wifi_twt_sdk_teardown_tx_matches_native(uint32_t identity, uintptr_t node, uint8_t flow);
/* Only reviewed individual/broadcast archive call sites are redirected. Other PM callers,
 * including IRAM paths, retain their original SDK functions. */
void esp32_mquickjs_wifi_twt_teardown_wake_up_native(void);
void esp32_mquickjs_wifi_twt_teardown_wake_done_native(void);
#endif
