#pragma once
#include "esp32_mquickjs_wifi_dpp_station.h"
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI && CONFIG_ESP_WIFI_DPP_SUPPORT && CONFIG_ESP_NETIF_USES_TCPIP_WITH_BSD_API && CONFIG_LWIP_IPV4
#include "esp_dpp.h"
#define ESP32_MQUICKJS_DPP_MAX_HANDLES 4U
typedef struct esp32_mquickjs_wifi_dpp_session esp32_mquickjs_wifi_dpp_session_t;
typedef struct {
    esp32_mquickjs_wifi_dpp_radio_status_t native;
    esp_err_t error, cleanup_error;
    const char *stage, *cleanup_stage;
    size_t reserved_bytes;
    uint16_t uri_length;
    uint8_t config_count;
    bool activated, worker_busy, uri_ready, uri_consumed;
    bool configs_ready, configs_consumed, closing, retired, timed_out;
    bool listening, capture_finished, native_closed, helper_drained;
    bool recovery_pending;
    bool connection_requested, connection_prepared, connection_established, connection_verified, connected;
    uint32_t configuration_index, connection_generation;
    int32_t connection_reason;
    esp32_mquickjs_wifi_dpp_auth_t authentication;
} esp32_mquickjs_wifi_dpp_session_status_t;
typedef struct {
    unsigned handles, workers;
    bool active, runtime_closing;
    esp32_mquickjs_wifi_dpp_session_status_t session;
} esp32_mquickjs_wifi_dpp_global_status_t;

esp_err_t esp32_mquickjs_wifi_dpp_open_runtime(void);
esp_err_t esp32_mquickjs_wifi_dpp_session_create(const esp32_mquickjs_wifi_dpp_worker_options_t *options,
    bool allow_ap_channel_change, uint32_t timeout_ms, esp32_mquickjs_wifi_dpp_session_t **out);
/* Runtime-only activation after JS construction. Public roots do not own the
 * independent background reference or permit uncertain IPC storage release. */
esp_err_t esp32_mquickjs_wifi_dpp_session_activate(esp32_mquickjs_wifi_dpp_session_t *session);
bool esp32_mquickjs_wifi_dpp_session_retain(esp32_mquickjs_wifi_dpp_session_t *session);
void esp32_mquickjs_wifi_dpp_session_release(esp32_mquickjs_wifi_dpp_session_t *session);
void esp32_mquickjs_wifi_dpp_session_close(esp32_mquickjs_wifi_dpp_session_t *session, bool timeout);
/* Runtime request only: the existing worker performs exact-source admission
 * and restoration. An in-flight request is idempotent; close waits for drain. */
esp_err_t esp32_mquickjs_wifi_dpp_session_recover(esp32_mquickjs_wifi_dpp_session_t *session);
bool esp32_mquickjs_wifi_dpp_session_status(esp32_mquickjs_wifi_dpp_session_t *session,
    esp32_mquickjs_wifi_dpp_session_status_t *status);
void esp32_mquickjs_wifi_dpp_global_status(esp32_mquickjs_wifi_dpp_global_status_t *status);
esp_err_t esp32_mquickjs_wifi_dpp_session_uri(esp32_mquickjs_wifi_dpp_session_t *session,
    char *uri, size_t capacity, bool commit);
/* Copied rows survive receipt commit for subsequent explicit selection. No
 * credentials enter status/watch. Close scrubs all retained rows and keys. */
esp_err_t esp32_mquickjs_wifi_dpp_session_config(esp32_mquickjs_wifi_dpp_session_t *session,
    unsigned index, esp_dpp_config_data_t *config);
esp_err_t esp32_mquickjs_wifi_dpp_session_configs_commit(esp32_mquickjs_wifi_dpp_session_t *session);
esp_err_t esp32_mquickjs_wifi_dpp_session_connect(esp32_mquickjs_wifi_dpp_session_t *session,
    unsigned index, esp32_mquickjs_wifi_dpp_auth_t authentication, uint32_t timeout_ms, bool allow_ap_restart);
esp_err_t esp32_mquickjs_wifi_dpp_session_connection_result(esp32_mquickjs_wifi_dpp_session_t *session,
    esp32_mquickjs_wifi_link_snapshot_t *link, double *elapsed_ms);
bool esp32_mquickjs_wifi_dpp_session_wait_begin(esp32_mquickjs_wifi_dpp_session_t *session, bool close);
void esp32_mquickjs_wifi_dpp_session_wait_end(esp32_mquickjs_wifi_dpp_session_t *session, bool close);
bool esp32_mquickjs_wifi_dpp_session_observation(esp32_mquickjs_wifi_dpp_session_t *session,
    esp32_mquickjs_wifi_dpp_session_status_t *status);
bool esp32_mquickjs_wifi_dpp_service(void);
bool esp32_mquickjs_wifi_dpp_prepare_runtime_destroy(void);
#endif
