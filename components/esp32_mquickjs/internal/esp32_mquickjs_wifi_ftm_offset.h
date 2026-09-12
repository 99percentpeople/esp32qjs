#pragma once
#include "sdkconfig.h"
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI && CONFIG_ESP_WIFI_FTM_ENABLE && CONFIG_ESP_WIFI_FTM_RESPONDER_SUPPORT && CONFIG_ESP_WIFI_SOFTAP_SUPPORT
#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint32_t generation, revision, accepted_revision;
    esp_err_t error;
    int16_t requested_cm, accepted_cm;
    bool configured, known, uncertain;
} esp32_mquickjs_wifi_ftm_offset_state_t;
typedef struct {
    uint32_t generation, revision;
    int16_t centimeters;
    bool configured;
} esp32_mquickjs_wifi_ftm_offset_snapshot_t;
typedef esp_err_t (*esp32_mquickjs_wifi_ftm_offset_writer_t)(void *, int16_t);

/* Caller owns the Radio mutation mutex and proves stopped AP/APSTA admission.
 * A failed SDK call may already have changed the write-only native value. */
esp_err_t esp32_mquickjs_wifi_ftm_offset_apply(esp32_mquickjs_wifi_ftm_offset_state_t *state,
    uint32_t generation, int16_t centimeters, esp32_mquickjs_wifi_ftm_offset_writer_t writer,
    void *opaque, bool *attempted);
/* Physical deinit or an admitted owner-free default reset revokes knowledge, never accepted history/revisions. */
void esp32_mquickjs_wifi_ftm_offset_invalidate(esp32_mquickjs_wifi_ftm_offset_state_t *state);
esp_err_t esp32_mquickjs_wifi_ftm_offset_capture(const esp32_mquickjs_wifi_ftm_offset_state_t *state,
    uint32_t generation, esp32_mquickjs_wifi_ftm_offset_snapshot_t *snapshot);
/* Only before AP START in a different physical generation. A successful suffix
 * is skipped only if its accepted record still matches this generation/value. */
esp_err_t esp32_mquickjs_wifi_ftm_offset_replay(esp32_mquickjs_wifi_ftm_offset_state_t *state,
    uint32_t generation, const esp32_mquickjs_wifi_ftm_offset_snapshot_t *snapshot,
    bool *completed, esp32_mquickjs_wifi_ftm_offset_writer_t writer, void *opaque);
#endif
