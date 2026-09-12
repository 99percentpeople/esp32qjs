#pragma once
#include "sdkconfig.h"
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI && CONFIG_SOC_WIFI_HE_SUPPORT && CONFIG_IDF_TARGET_ESP32C5
#include "esp_err.h"
#include "esp_wifi_he_types.h"
#include <stdbool.h>
#include <stddef.h>
/* Synchronous native call scope, never a callback argument or JS root. The
 * native caller keeps it alive through end(). Normalized value data only. */
typedef struct esp32_mquickjs_wifi_btwt_event_scope {
    union { wifi_event_sta_btwt_setup_t setup; wifi_event_sta_btwt_teardown_t teardown; } event;
    struct esp32_mquickjs_wifi_btwt_event_scope *previous;
    int event_id;
    esp_err_t error;
    uint8_t broadcast_id;
    bool seen, ambiguous;
} esp32_mquickjs_wifi_btwt_event_scope_t;
bool esp32_mquickjs_wifi_btwt_event_begin(esp32_mquickjs_wifi_btwt_event_scope_t *, int event_id, uint8_t broadcast_id);
bool esp32_mquickjs_wifi_btwt_event_end(esp32_mquickjs_wifi_btwt_event_scope_t *);
/* Called only AFTER the caller records its native terminal state. Zero-wait;
 * observation failure does not change the saved native outcome. */
esp_err_t esp32_mquickjs_wifi_btwt_event_publish(const esp32_mquickjs_wifi_btwt_event_scope_t *);
esp_err_t esp32_mquickjs_wifi_btwt_event_post(int event_id, const void *data, size_t size);
#endif
