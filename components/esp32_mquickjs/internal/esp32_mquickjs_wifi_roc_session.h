#pragma once
#include "esp32_mquickjs_wifi_action_radio.h"
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI
#define ESP32_MQUICKJS_WIFI_ROC_MAX_HANDLES 8U
typedef struct esp32_mquickjs_wifi_roc_session esp32_mquickjs_wifi_roc_session_t;
typedef struct {
    wifi_roc_req_t request;
    esp32_mquickjs_wifi_action_lane_t native;
    bool started, submitted, close_requested, retired;
    esp_err_t error, cleanup_error;
    const char *stage, *cleanup_stage;
} esp32_mquickjs_wifi_roc_status_t;
/* create/retain/release own native references only. Creation does no driver I/O.
 * Closed retained handles consume the same bounded budget until last release. */
esp_err_t esp32_mquickjs_wifi_roc_create(const wifi_roc_req_t *, esp32_mquickjs_wifi_roc_session_t **);
bool esp32_mquickjs_wifi_roc_retain(esp32_mquickjs_wifi_roc_session_t *);
void esp32_mquickjs_wifi_roc_release(esp32_mquickjs_wifi_roc_session_t *);
esp_err_t esp32_mquickjs_wifi_roc_start(esp32_mquickjs_wifi_roc_session_t *);
void esp32_mquickjs_wifi_roc_close(esp32_mquickjs_wifi_roc_session_t *);
bool esp32_mquickjs_wifi_roc_status(esp32_mquickjs_wifi_roc_session_t *, esp32_mquickjs_wifi_roc_status_t *);
bool esp32_mquickjs_wifi_roc_service(void);
bool esp32_mquickjs_wifi_roc_prepare_runtime_destroy(void);
bool esp32_mquickjs_wifi_roc_current_status(esp32_mquickjs_wifi_roc_status_t *);
void esp32_mquickjs_wifi_roc_counts(uint32_t *handles, bool *active, bool *cleanup);
#endif
