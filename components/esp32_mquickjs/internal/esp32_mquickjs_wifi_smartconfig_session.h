#pragma once
#include "esp32_mquickjs_wifi_smartconfig_radio.h"
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI && CONFIG_ESP_NETIF_USES_TCPIP_WITH_BSD_API && CONFIG_LWIP_IPV4
#define ESP32_MQUICKJS_SMARTCONFIG_MAX_HANDLES 4U

typedef struct esp32_mquickjs_wifi_smartconfig_session esp32_mquickjs_wifi_smartconfig_session_t;
typedef struct {
    uint32_t timeout_ms;
    wifi_auth_mode_t minimum_auth;
    bool allow_open, pmf_required;
} esp32_mquickjs_wifi_smartconfig_connection_options_t;
typedef struct {
    esp32_mquickjs_wifi_smartconfig_radio_status_t native;
    esp_err_t error, cleanup_error;
    const char *stage, *cleanup_stage;
    size_t reserved_bytes;
    bool activated, worker_busy, credentials_ready, credentials_consumed;
    bool closing, retired, timed_out, ack_requested;
    bool auto_connect, connection_started, connection_transferred, completed;
    uint32_t connection_generation;
    int32_t connection_reason;
} esp32_mquickjs_wifi_smartconfig_session_status_t;

/* Metadata-only atomic observation, also available without a public handle. */
typedef struct {
    unsigned handles, workers;
    bool active, runtime_closing;
    esp32_mquickjs_wifi_smartconfig_session_status_t session;
} esp32_mquickjs_wifi_smartconfig_global_status_t;
void esp32_mquickjs_wifi_smartconfig_global_status(esp32_mquickjs_wifi_smartconfig_global_status_t *status);

/* Runtime admission and background service. */
esp_err_t esp32_mquickjs_wifi_smartconfig_open_runtime(void);
esp_err_t esp32_mquickjs_wifi_smartconfig_session_create(
    const esp32_mquickjs_wifi_smartconfig_decoder_options_t *options,
    bool allow_ap_channel_change, uint32_t timeout_ms,
    esp32_mquickjs_wifi_smartconfig_session_t **out);
esp_err_t esp32_mquickjs_wifi_smartconfig_connection_validate_options(
    const esp32_mquickjs_wifi_smartconfig_connection_options_t *options);
/* Runtime task only, before activation; no driver mutation. */
esp_err_t esp32_mquickjs_wifi_smartconfig_session_configure_connection(
    esp32_mquickjs_wifi_smartconfig_session_t *session,
    const esp32_mquickjs_wifi_smartconfig_connection_options_t *options);
/* Activate only after the public JS object is successfully constructed. */
esp_err_t esp32_mquickjs_wifi_smartconfig_session_activate(esp32_mquickjs_wifi_smartconfig_session_t *session);
bool esp32_mquickjs_wifi_smartconfig_session_retain(esp32_mquickjs_wifi_smartconfig_session_t *session);
void esp32_mquickjs_wifi_smartconfig_session_release(esp32_mquickjs_wifi_smartconfig_session_t *session);
void esp32_mquickjs_wifi_smartconfig_session_close(esp32_mquickjs_wifi_smartconfig_session_t *session, bool timeout);
bool esp32_mquickjs_wifi_smartconfig_session_status(esp32_mquickjs_wifi_smartconfig_session_t *session,
    esp32_mquickjs_wifi_smartconfig_session_status_t *status);
/* Public Future capture/destroy balance these counters. An observer may not
 * publish a ready result before the matching captured Future is settled. */
bool esp32_mquickjs_wifi_smartconfig_session_wait_begin(esp32_mquickjs_wifi_smartconfig_session_t *, bool close);
void esp32_mquickjs_wifi_smartconfig_session_wait_end(esp32_mquickjs_wifi_smartconfig_session_t *, bool close);
bool esp32_mquickjs_wifi_smartconfig_session_observation(esp32_mquickjs_wifi_smartconfig_session_t *,
    esp32_mquickjs_wifi_smartconfig_session_status_t *);
/* Runtime poller/teardown only. Closing a watcher releases its Session reference;
 * the existing final-owner policy may then request native Session close. */
bool esp32_mquickjs_wifi_smartconfig_poll_observations(bool close);
/* These only touch retained native bytes; no SDK or Radio lock on the JS task.
 * Commit follows successful conversion. A failed conversion may copy again. */
esp_err_t esp32_mquickjs_wifi_smartconfig_session_credentials(esp32_mquickjs_wifi_smartconfig_session_t *session,
    esp32_mquickjs_wifi_smartconfig_credentials_t *credentials, bool commit);
esp_err_t esp32_mquickjs_wifi_smartconfig_session_ack(esp32_mquickjs_wifi_smartconfig_session_t *session);
bool esp32_mquickjs_wifi_smartconfig_service(void);
bool esp32_mquickjs_wifi_smartconfig_prepare_runtime_destroy(void);
#endif
