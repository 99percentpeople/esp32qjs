#pragma once
#include "sdkconfig.h"
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI
#include "esp32_mquickjs_wifi_wps_sdk.h"

typedef struct esp32_mquickjs_wifi_wps_worker esp32_mquickjs_wifi_wps_worker_t;
typedef struct {
    esp32_mquickjs_wifi_wps_native_status_t native;
    esp_err_t error, cleanup_error;
    size_t reserved_bytes;
    const char *stage;
    bool closing, capture_retired, retired, handoff_unknown;
} esp32_mquickjs_wifi_wps_worker_status_t;

/* Serialized worker calls only; not an ISR/native Wi-Fi callback or JS thread
 * API. The future Radio owner must exclusively hold STARTED Station, connection
 * and scan admission through close/release, and drain its boot event history
 * before permitting another Station operation. Public Future storage must NOT
 * own this object: unknown native handoff or cleanup failure retains it.
 * create validates and copies only, without driver mutation. */
esp_err_t esp32_mquickjs_wifi_wps_worker_create(const esp_wps_config_t *config,
    esp32_mquickjs_wifi_wps_worker_t **out);
bool esp32_mquickjs_wifi_wps_config_valid(const esp_wps_config_t *config);
/* Reserve/enable native WPS without scanning or changing Station config.
 * Radio sets temporary RAM storage only after this succeeds, then starts. */
esp_err_t esp32_mquickjs_wifi_wps_worker_prepare(esp32_mquickjs_wifi_wps_worker_t *worker);
esp_err_t esp32_mquickjs_wifi_wps_worker_start(esp32_mquickjs_wifi_wps_worker_t *worker);
esp_err_t esp32_mquickjs_wifi_wps_worker_status(esp32_mquickjs_wifi_wps_worker_t *worker,
    esp32_mquickjs_wifi_wps_worker_status_t *status);
/* Terminal capture cleanup preserves the result until copy/commit or close. */
esp_err_t esp32_mquickjs_wifi_wps_worker_finish_capture(esp32_mquickjs_wifi_wps_worker_t *worker);
esp_err_t esp32_mquickjs_wifi_wps_worker_pin(esp32_mquickjs_wifi_wps_worker_t *worker,
    uint8_t pin[8], bool commit);
esp_err_t esp32_mquickjs_wifi_wps_worker_credentials(esp32_mquickjs_wifi_wps_worker_t *worker,
    esp32_mquickjs_wifi_wps_credentials_t *credentials, bool commit);
esp_err_t esp32_mquickjs_wifi_wps_worker_close(esp32_mquickjs_wifi_wps_worker_t *worker);
esp_err_t esp32_mquickjs_wifi_wps_worker_release(esp32_mquickjs_wifi_wps_worker_t **worker);
#endif
