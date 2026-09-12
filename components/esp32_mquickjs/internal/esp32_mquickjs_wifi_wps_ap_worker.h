#pragma once
#include "sdkconfig.h"
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI && CONFIG_ESP_WIFI_WPS_SOFTAP_REGISTRAR
#include "esp32_mquickjs_wifi_wps_ap_sdk.h"

typedef struct esp32_mquickjs_wifi_wps_ap_worker esp32_mquickjs_wifi_wps_ap_worker_t;
typedef struct {
    esp32_mquickjs_wifi_wps_ap_result_status_t native;
    esp_err_t error, cleanup_error;
    size_t reserved_bytes;
    const char *stage;
    bool closing, capture_retired, retired, handoff_unknown;
} esp32_mquickjs_wifi_wps_ap_worker_status_t;

/* Serialized background calls only. Radio must hold the exact AP lifecycle,
 * configuration and WPS admission until release. An unresolved IPC dispatch
 * retains this entire object independently of any public Future or JS root.
 * create only validates/copies; it does not reserve or mutate a driver. */
esp_err_t esp32_mquickjs_wifi_wps_ap_worker_create(const esp_wps_config_t *config,
    esp32_mquickjs_wifi_wps_ap_worker_t **out);
esp_err_t esp32_mquickjs_wifi_wps_ap_worker_prepare(esp32_mquickjs_wifi_wps_ap_worker_t *worker);
esp_err_t esp32_mquickjs_wifi_wps_ap_worker_start(esp32_mquickjs_wifi_wps_ap_worker_t *worker);
esp_err_t esp32_mquickjs_wifi_wps_ap_worker_status(esp32_mquickjs_wifi_wps_ap_worker_t *worker,
    esp32_mquickjs_wifi_wps_ap_worker_status_t *status);
esp_err_t esp32_mquickjs_wifi_wps_ap_worker_pin(esp32_mquickjs_wifi_wps_ap_worker_t *worker,
    uint8_t pin[8], bool commit);
/* Retires SDK state after a terminal result; keeps metadata until close. */
esp_err_t esp32_mquickjs_wifi_wps_ap_worker_finish_capture(esp32_mquickjs_wifi_wps_ap_worker_t *worker);
esp_err_t esp32_mquickjs_wifi_wps_ap_worker_close(esp32_mquickjs_wifi_wps_ap_worker_t *worker);
esp_err_t esp32_mquickjs_wifi_wps_ap_worker_release(esp32_mquickjs_wifi_wps_ap_worker_t **worker);
#endif
