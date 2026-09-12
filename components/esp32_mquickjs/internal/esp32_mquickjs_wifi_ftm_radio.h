#pragma once
#include "sdkconfig.h"
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI && CONFIG_ESP_WIFI_FTM_ENABLE && CONFIG_ESP_WIFI_FTM_INITIATOR_SUPPORT
#include "esp_wifi.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define ESP32_MQUICKJS_WIFI_FTM_MAX_REPORT_ENTRIES 64U
typedef struct { uint32_t generation, identity; } esp32_mquickjs_wifi_ftm_token_t;
/* Boot-owned native control snapshot. No JS value, caller buffer or SDK pointer.
 * report contains only initialized summary fields; timestamps stay in the typed
 * caller-owned native entries and must not be converted to lossy JS numbers. */
typedef struct {
    esp32_mquickjs_wifi_ftm_token_t token;
    uint32_t revision;
    wifi_event_ftm_report_t report;
    esp_err_t submit_error, end_error, report_error;
    uint8_t copied_entries;
    bool dispatching, submitted, end_requested, end_written;
    bool terminal, ambiguous, report_consumed, report_discarded;
    bool sdk_fenced, event_fenced;
    bool physical_termination;
} esp32_mquickjs_wifi_ftm_state_t;

bool esp32_mquickjs_wifi_ftm_config_valid(const wifi_ftm_initiator_cfg_t *config);

/* Internal Radio API, not a registered JS contract. Requires an already started
 * Station interface. The driver stores a boot-owned copy of config until exact
 * retirement. Admission returns a token even if subsequent SDK submission fails;
 * callers must keep polling/closing that owner, never retry initiation.
 * Off-channel is limited to disconnected Station with no other RF-dependent
 * owner; associated Station/APSTA may measure on their current primary channel. */
esp_err_t esp32_mquickjs_wifi_radio_ftm_start(const wifi_ftm_initiator_cfg_t *config,
    esp32_mquickjs_wifi_ftm_token_t *token);
/* End acceptance is not terminal. Retry only a failed end suffix. */
esp_err_t esp32_mquickjs_wifi_radio_ftm_end(const esp32_mquickjs_wifi_ftm_token_t *token);
bool esp32_mquickjs_wifi_radio_ftm_status(const esp32_mquickjs_wifi_ftm_token_t *token,
    esp32_mquickjs_wifi_ftm_state_t *out);
void esp32_mquickjs_wifi_radio_ftm_snapshot(esp32_mquickjs_wifi_ftm_state_t *out);
/* One destructive SDK report read after native termination/fence. NULL/0 means
 * discard; otherwise capacity is 1..64 native entries, bytes must exactly match.
 * No allocation and no stored caller pointer. Only out->copied_entries are valid
 * on ESP_OK; source count remains in out->report for explicit truncation.
 * A successful read cannot be repeated, including after JS conversion failure.
 * Failed reads retain cleanup responsibility and may be retried or discarded. */
esp_err_t esp32_mquickjs_wifi_radio_ftm_collect(const esp32_mquickjs_wifi_ftm_token_t *token,
    wifi_ftm_report_entry_t *entries, size_t bytes, unsigned capacity,
    esp32_mquickjs_wifi_ftm_state_t *out);
/* Nonblocking default-loop drain after report consumption. TIMEOUT retains the
 * exact lease; successful retirement clears token. Missing/conflicting native
 * reports remain quarantined: timeout/end/runtime teardown cannot prove release.
 * A physical recovery coordinator is still required for those cases. */
esp_err_t esp32_mquickjs_wifi_radio_ftm_retire(esp32_mquickjs_wifi_ftm_token_t *token,
    esp32_mquickjs_wifi_ftm_state_t *out);

#endif
