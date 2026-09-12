#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "sdkconfig.h"

/* Internal SDK bridge, not a public NAN Session contract. The Radio coordinator
 * must own NAN exclusively and establish native/event fences before reuse.
 * A notice is protocol observation, not proof that TX buffers were freed. */
typedef enum {
    ESP32_MQUICKJS_NAN_SDK_EVENT,
    ESP32_MQUICKJS_NAN_SDK_STARTED,
    ESP32_MQUICKJS_NAN_SDK_STOPPED,
    ESP32_MQUICKJS_NAN_SDK_TX_DONE,
    ESP32_MQUICKJS_NAN_SDK_NDP_REQUEST,
    ESP32_MQUICKJS_NAN_SDK_NDP_REJECTED,
    ESP32_MQUICKJS_NAN_SDK_NDP_TERMINATED,
    ESP32_MQUICKJS_NAN_SDK_SERVICE_BOUND,
    ESP32_MQUICKJS_NAN_SDK_DISCOVERY_NDP_DENIED,
    ESP32_MQUICKJS_NAN_SDK_NDP_ACCEPTED,
    ESP32_MQUICKJS_NAN_SDK_NDP_FAILED,
    ESP32_MQUICKJS_NAN_SDK_NDP_CLEANUP_FAILED,
    ESP32_MQUICKJS_NAN_SDK_NDP_BOUND,
    ESP32_MQUICKJS_NAN_SDK_NDP_DELETED,
    ESP32_MQUICKJS_NAN_SDK_SERVICE_TERMINATED,
    ESP32_MQUICKJS_NAN_SDK_BOOTSTRAP,
} esp32_mquickjs_wifi_nan_sdk_notice_kind_t;

/* Borrowed only during NDP_REQUEST/NDP_ACCEPTED delivery. All metadata is
 * scalar; ssi is separately borrowed from the native frame and must be copied
 * by a bounded admission owner before returning. No keys are exposed. */
typedef struct {
    uint8_t peer_ndi[6], own_ndi[6], ipv6_identifier[8];
    const uint8_t *ssi;
    uint16_t ssi_len;
} esp32_mquickjs_wifi_nan_sdk_ndp_data_t;

typedef struct {
    uint8_t type, peer_service_id, status;
    uint16_t methods;
} esp32_mquickjs_wifi_nan_sdk_bootstrap_t;
enum { ESP32_MQUICKJS_NAN_BOOTSTRAP_REQUEST = 1, ESP32_MQUICKJS_NAN_BOOTSTRAP_RESPONSE = 2 };
enum { ESP32_MQUICKJS_NAN_BOOTSTRAP_ACCEPTED, ESP32_MQUICKJS_NAN_BOOTSTRAP_REJECTED, ESP32_MQUICKJS_NAN_BOOTSTRAP_COMEBACK };

typedef struct {
    esp32_mquickjs_wifi_nan_sdk_notice_kind_t kind;
    uint32_t context;
    int32_t event_id, status;
    uint8_t service_id, ndp_id, peer[6];
    const void *data;
    size_t size;
} esp32_mquickjs_wifi_nan_sdk_notice_t;

/* May run with the SDK NAN data mutex held. Only bounded native state capture
 * is allowed: no JS, SDK calls, blocking, observer detach or caller free.
 * data and the notice are borrowed only until this callback returns. */
typedef void (*esp32_mquickjs_wifi_nan_sdk_observer_fn)(uint32_t identity,
    const esp32_mquickjs_wifi_nan_sdk_notice_t *notice, void *opaque);

typedef struct {
    uint32_t identity, callbacks, dropped_events;
    uint32_t service_callbacks;
    bool closing;
} esp32_mquickjs_wifi_nan_sdk_observer_status_t;

#if CONFIG_ESP_WIFI_NAN_SYNC_ENABLE
/* Freeze the complete match/replied/receive/NDP-indication app callbacks AND
 * the native SD TX callback's service branch, including code after the app callback returns. The count
 * includes nested native/app scopes. id=0 freezes all services. A timeout retains the freeze; no SDK
 * mutation may run until quiescence. Resume requires externally proven native
 * cancellation/buffer retirement, or a stopped/reset new discovery lifetime.
 * Global quiescence also freezes complete NDP app, native NAF RX/TX, null-data
 * TX and timer-handler scopes; service_callbacks includes those scopes. The
 * numeric timer ledger separately revokes queued callbacks and retains failed
 * cleanup handles. This does not establish data-plane buffer/RF retirement. */
esp_err_t esp32_mquickjs_wifi_nan_sdk_service_quiesce(uint8_t id);
esp_err_t esp32_mquickjs_wifi_nan_sdk_service_resume(uint8_t id);
#endif

/* Outputs a boot-unique identity. Does not initialize/start NAN or allocate.
 * No second observer can replace a live or still-draining registration. */
esp_err_t esp32_mquickjs_wifi_nan_sdk_observe(
    esp32_mquickjs_wifi_nan_sdk_observer_fn callback, void *opaque, uint32_t *identity);
/* Revokes admission before returning TIMEOUT for an entered callback. The caller
 * retains opaque/storage and retries this suffix. Old identities cannot detach
 * a newer observer. This does not stop NAN or retire native service IDs. */
esp_err_t esp32_mquickjs_wifi_nan_sdk_unobserve(uint32_t *identity);
void esp32_mquickjs_wifi_nan_sdk_observer_status(
    esp32_mquickjs_wifi_nan_sdk_observer_status_t *output);
/* Build-local USD engine forwards bounded borrowed native notices through
 * this same observer. No independent callback registry or JS roots. */
void esp32_mquickjs_wifi_nan_sdk_notice(const esp32_mquickjs_wifi_nan_sdk_notice_t *notice);

#if CONFIG_ESP_WIFI_NAN_SYNC_ENABLE
#include "esp_wifi_types.h"
/* Radio mutation owner only. Prepare uses the SDK's real synchronization
 * configuration path, but leaves START/STOP submission to Radio. Reset is
 * legal only after physical STOP and its default-loop fence (or before the
 * first START attempt). Neither function grants ownership. */
esp_err_t esp32_mquickjs_wifi_nan_sdk_sync_prepare(const wifi_nan_sync_config_t *config);
esp_err_t esp32_mquickjs_wifi_nan_sdk_sync_reset(void);
#if CONFIG_ESP_WIFI_NAN_SECURITY
/* Serialized Wi-Fi NAF RX boundaries, or physical STOP after RX retirement. */
void esp32_mquickjs_wifi_nan_sdk_security_clear_pending(void);
esp_err_t esp32_mquickjs_wifi_nan_sdk_security_install_group_keys(void);
#endif
esp_err_t esp32_mquickjs_wifi_nan_sdk_sync_ready(void);
/* Exclusive Radio mutation owner only. Config and nested inputs stay live
 * through the call. These preserve raw native submit errors; the public SDK
 * publish/subscribe functions continue to return their original byte ID.
 * No new service may start while an old native ID is awaiting TX retirement. */
esp_err_t esp32_mquickjs_wifi_nan_sdk_publish(const wifi_nan_publish_cfg_t *config, uint8_t *id);
esp_err_t esp32_mquickjs_wifi_nan_sdk_subscribe(const wifi_nan_subscribe_cfg_t *config, uint8_t *id);
/* Success cancels native/host service state, but is NOT buffer retirement.
 * Keep the service identity frozen and retain its framework record until the
 * TX ledger drains. Do not resubmit a successful cancel on a later retry. */
esp_err_t esp32_mquickjs_wifi_nan_sdk_cancel_service(uint8_t id);
/* Submit only, without the SDK's shared event-group wait or payload logging.
 * Exact peer/service IDs are mandatory; params/context stay live through ioctl. */
esp_err_t esp32_mquickjs_wifi_nan_sdk_send(wifi_nan_followup_params_t *params, uint32_t *context);
#if CONFIG_ESP_WIFI_NAN_PAIRING
esp_err_t esp32_mquickjs_wifi_nan_sdk_bootstrap_send(wifi_nan_followup_params_t *params,
    uint32_t *context, bool response, bool accept);
#endif
#endif
