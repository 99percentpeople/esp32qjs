#pragma once
#include "esp32_mquickjs_wifi_smartconfig_timer.h"
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI && CONFIG_ESP_NETIF_USES_TCPIP_WITH_BSD_API && CONFIG_LWIP_IPV4

typedef struct esp32_mquickjs_wifi_smartconfig_decoder esp32_mquickjs_wifi_smartconfig_decoder_t;
typedef struct {
    smartconfig_type_t type;
    uint8_t channel_timeout_s; /* SDK find-channel timeout, 15..255; not a JS deadline. */
    bool fast_mode;
    const uint8_t *key;
    size_t key_length; /* SDK string key: exactly 16 non-NUL bytes, or absent. */
} esp32_mquickjs_wifi_smartconfig_decoder_options_t;
typedef struct {
    esp32_mquickjs_wifi_smartconfig_token_t token;
    uint64_t ack_identity;
    size_t reserved_bytes;
    esp_err_t error, cleanup_error, ack_error, ack_observation_error;
    int ack_socket_errno;
    const char *stage;
    bool started, capture_stopped, closing, retired, handoff_unknown, ack_busy, ack_completed, mutation_attempted;
} esp32_mquickjs_wifi_smartconfig_decoder_status_t;

/* Internal worker coordinator, NOT a public Session or Radio admission API.
 * Caller holds exclusive STARTED Radio ownership through release, serializes ALL calls
 * including status, and keeps this object beyond Future cancellation/timeout.
 * Create validates/copies inputs without driver mutation. After any start error
 * call close until success; release never silently drops native ownership. */
esp_err_t esp32_mquickjs_wifi_smartconfig_decoder_create(uint32_t generation,
    const esp32_mquickjs_wifi_smartconfig_decoder_options_t *options,
    esp32_mquickjs_wifi_smartconfig_decoder_t **out);
esp_err_t esp32_mquickjs_wifi_smartconfig_decoder_validate_options(
    const esp32_mquickjs_wifi_smartconfig_decoder_options_t *options);
esp_err_t esp32_mquickjs_wifi_smartconfig_decoder_start(esp32_mquickjs_wifi_smartconfig_decoder_t *decoder);
esp_err_t esp32_mquickjs_wifi_smartconfig_decoder_status(esp32_mquickjs_wifi_smartconfig_decoder_t *decoder,
    esp32_mquickjs_wifi_smartconfig_decoder_status_t *status);
/* Retire RF capture while retaining credentials and the ACK reservation. This
 * is the boundary before an explicitly authorized Station connection/ACK; it
 * does not release the Radio lease or consume the credential. */
esp_err_t esp32_mquickjs_wifi_smartconfig_decoder_finish_capture(esp32_mquickjs_wifi_smartconfig_decoder_t *decoder);
/* Caller explicitly decides when to ACK (e.g. after acquiring an IP). This
 * function requires retired capture and submits ONCE. Credential copy retains
 * the small private ACK metadata so a successful credential commit does not
 * destroy a later explicitly requested ACK handoff.
 * It does not consume the credential or implicitly connect the Station. */
esp_err_t esp32_mquickjs_wifi_smartconfig_decoder_ack(esp32_mquickjs_wifi_smartconfig_decoder_t *decoder);
esp_err_t esp32_mquickjs_wifi_smartconfig_decoder_credentials(esp32_mquickjs_wifi_smartconfig_decoder_t *decoder,
    esp32_mquickjs_wifi_smartconfig_credentials_t *credentials, bool commit);
esp_err_t esp32_mquickjs_wifi_smartconfig_decoder_close(esp32_mquickjs_wifi_smartconfig_decoder_t *decoder);
esp_err_t esp32_mquickjs_wifi_smartconfig_decoder_release(esp32_mquickjs_wifi_smartconfig_decoder_t **decoder);
#endif
