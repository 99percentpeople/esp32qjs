#pragma once
#include "sdkconfig.h"

#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI && CONFIG_ESP_NETIF_USES_TCPIP_WITH_BSD_API && CONFIG_LWIP_IPV4
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "esp_smartconfig.h"

typedef struct {
    uint64_t identity;
    uint32_t radio_generation;
} esp32_mquickjs_wifi_smartconfig_token_t;

#define ESP32_MQUICKJS_SMARTCONFIG_CUSTOM_DATA_MAX 64
/* One atomic copy/commit owner for network credentials and ESPTouch v2 binary
 * custom data. Non-v2 credentials have zero custom length. Never stringify. */
typedef struct {
    smartconfig_event_got_ssid_pswd_t network;
    uint8_t custom_length;
    uint8_t custom_data[ESP32_MQUICKJS_SMARTCONFIG_CUSTOM_DATA_MAX];
} esp32_mquickjs_wifi_smartconfig_credentials_t;

/* Metadata only. No SSID, password, phone token/address or custom data. ACK
 * observations are NOT completion proof; poll the exact SDK ACK identity. */
typedef struct {
    esp32_mquickjs_wifi_smartconfig_token_t token;
    uint32_t captured_events, duplicate_credentials, discarded_events;
    uint32_t reserved_bytes;
    esp_err_t event_error, allocation_error;
    bool closing, scan_done, channel_found, credentials_received;
    bool credentials_consumed, ack_observed;
} esp32_mquickjs_wifi_smartconfig_events_status_t;

/* Reserve before calling the decoder. The native record outlives public Future
 * or JS storage. Only one decoder owner can hold it, including during cleanup.
 * The caller must serialize actual decoder and Radio operations separately. */
esp_err_t esp32_mquickjs_wifi_smartconfig_events_begin(uint32_t radio_generation,
    esp32_mquickjs_wifi_smartconfig_token_t *token);
esp_err_t esp32_mquickjs_wifi_smartconfig_events_status(
    const esp32_mquickjs_wifi_smartconfig_token_t *token,
    esp32_mquickjs_wifi_smartconfig_events_status_t *status);

/* A copy is not a transfer: commit only after successful public conversion or
 * native connection handoff. Conversion OOM can retry the original credential.
 * The caller owns this copy and must secure-zero it on every exit. */
esp_err_t esp32_mquickjs_wifi_smartconfig_credentials_copy(
    const esp32_mquickjs_wifi_smartconfig_token_t *token,
    esp32_mquickjs_wifi_smartconfig_credentials_t *credentials);
esp_err_t esp32_mquickjs_wifi_smartconfig_credentials_commit(
    const esp32_mquickjs_wifi_smartconfig_token_t *token);

/* Close revokes delivery and scrubs the retained credential. Continue absorbing
 * late events until the owner proves decoder/timer/native/ACK retirement.
 * release is internal: only call AFTER those external lifetime obligations are
 * proven; a successful esp_smartconfig_stop alone is not that proof. */
esp_err_t esp32_mquickjs_wifi_smartconfig_events_close(
    const esp32_mquickjs_wifi_smartconfig_token_t *token);
esp_err_t esp32_mquickjs_wifi_smartconfig_events_release(
    esp32_mquickjs_wifi_smartconfig_token_t *token);

/* Native post boundary. True means a reserved owner consumed this event without
 * enqueueing credentials or waiting on the default observation queue. With no
 * owner, false preserves the SDK caller's original event behavior. No SDK/JS,
 * heap allocation, native stop, ACK launch or queue send occurs in capture. */
bool esp32_mquickjs_wifi_smartconfig_event_capture(int32_t event_id,
    const void *data, size_t length);

/* Synchronous hook from the isolated SDK decoder allocator on its native task.
 * The record remains reserved until decoder/native queue retirement. No cookie
 * or delayed callback is retained; unmanaged/closing decoders are ignored. */
void esp32_mquickjs_wifi_smartconfig_allocation_failed(void);
#endif
