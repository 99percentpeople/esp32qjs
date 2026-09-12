#pragma once
#include "sdkconfig.h"
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI && CONFIG_ESP_WIFI_DPP_SUPPORT
#include "esp32_mquickjs_wifi_dpp_result.h"
#include "esp32_mquickjs_wifi_dpp_connection.h"

typedef struct esp32_mquickjs_wifi_dpp_worker esp32_mquickjs_wifi_dpp_worker_t;
typedef struct {
    char channels[ESP32QJS_DPP_CHANNEL_LIST_MAX];
    char private_key_hex_der[2U * ESP32QJS_DPP_KEY_DER_MAX + 1U];
    char info[ESP32QJS_DPP_INFO_MAX + 1U];
    bool has_key, has_info;
} esp32_mquickjs_wifi_dpp_worker_options_t;
typedef struct {
    esp32_mquickjs_wifi_dpp_result_status_t native;
    esp_err_t error, cleanup_error;
    size_t reserved_bytes;
    const char *stage;
    bool closing, capture_retired, retired, handoff_unknown;
    bool selection_attempted, connection_selected;
} esp32_mquickjs_wifi_dpp_worker_status_t;

/* Serialized background calls, under the caller's Radio/lifecycle admission.
 * All native IPC arguments/results are owned by this object. An unresolved
 * handoff keeps the entire object independently of JS roots or a Future. */
esp_err_t esp32_mquickjs_wifi_dpp_worker_validate(const esp32_mquickjs_wifi_dpp_worker_options_t *options);
esp_err_t esp32_mquickjs_wifi_dpp_worker_create(const esp32_mquickjs_wifi_dpp_worker_options_t *options,
    esp32_mquickjs_wifi_dpp_worker_t **out);
esp_err_t esp32_mquickjs_wifi_dpp_worker_prepare(esp32_mquickjs_wifi_dpp_worker_t *worker);
esp_err_t esp32_mquickjs_wifi_dpp_worker_listen(esp32_mquickjs_wifi_dpp_worker_t *worker);
esp_err_t esp32_mquickjs_wifi_dpp_worker_status(esp32_mquickjs_wifi_dpp_worker_t *worker,
    esp32_mquickjs_wifi_dpp_worker_status_t *status);
esp_err_t esp32_mquickjs_wifi_dpp_worker_uri(esp32_mquickjs_wifi_dpp_worker_t *worker,
    char *out, size_t capacity, bool commit);
esp_err_t esp32_mquickjs_wifi_dpp_worker_config(esp32_mquickjs_wifi_dpp_worker_t *worker,
    unsigned index, esp_dpp_config_data_t *out);
esp_err_t esp32_mquickjs_wifi_dpp_worker_configs_commit(esp32_mquickjs_wifi_dpp_worker_t *worker);
/* One explicit selection, after copied capture results have retired. A new
 * native identity prevents provisioning terminal state/timers from becoming a
 * connection result. Failures retain the exact unfinished cleanup suffix. */
esp_err_t esp32_mquickjs_wifi_dpp_worker_select(esp32_mquickjs_wifi_dpp_worker_t *worker,
    const esp_dpp_config_data_t *row, esp32_mquickjs_wifi_dpp_auth_t requested,
    esp32_mquickjs_wifi_dpp_auth_t *selected, wifi_config_t *station);
esp_err_t esp32_mquickjs_wifi_dpp_worker_check_connection(esp32_mquickjs_wifi_dpp_worker_t *worker,
    esp32_mquickjs_wifi_dpp_auth_t selected, const uint8_t bssid[6]);
/* Stop protocol/SDK work after a terminal result, retaining copied results. */
esp_err_t esp32_mquickjs_wifi_dpp_worker_finish_capture(esp32_mquickjs_wifi_dpp_worker_t *worker);
esp_err_t esp32_mquickjs_wifi_dpp_worker_close(esp32_mquickjs_wifi_dpp_worker_t *worker);
esp_err_t esp32_mquickjs_wifi_dpp_worker_release(esp32_mquickjs_wifi_dpp_worker_t **worker);
#endif
