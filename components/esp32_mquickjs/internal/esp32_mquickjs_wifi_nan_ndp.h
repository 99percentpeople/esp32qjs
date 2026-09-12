#pragma once
#include "esp32_mquickjs_wifi_nan_sdk.h"
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI && CONFIG_ESP_WIFI_NAN_SYNC_ENABLE
/* Internal native operation ledger. These are not JS handles or public API
 * declarations. Records share the existing NAN control allocation. Individual
 * release runs on the native Wi-Fi task after deletion and buffer retirement;
 * parent STOP remains responsible for incomplete native cleanup. */
typedef struct {
    uint32_t identity;
    int64_t completed_us, started_us;
    uint8_t service_id, publisher_id, ndp_id, peer[6];
    uint8_t peer_ndi[6], own_ndi[6], ipv6_identifier[8];
    int32_t native_status;
    esp_err_t error, cleanup_error, submit_error;
    bool outgoing, native_bound, host_bound, submitted, response_attempted;
    bool end_attempted, end_submitted, accepted, rejected, terminated, native_deleted;
    bool frames_retired;
} esp32_mquickjs_wifi_nan_ndp_status_t;

esp_err_t esp32_mquickjs_wifi_nan_ndp_begin(uint8_t service_id,
    wifi_nan_datapath_req_t *request, uint32_t *identity);
bool esp32_mquickjs_wifi_nan_ndp_status(uint32_t identity,
    esp32_mquickjs_wifi_nan_ndp_status_t *status);
unsigned esp32_mquickjs_wifi_nan_ndp_list(esp32_mquickjs_wifi_nan_ndp_status_t status[ESP_WIFI_NAN_DATAPATH_MAX_PEERS]);
bool esp32_mquickjs_wifi_nan_ndp_ssi(uint32_t identity, uint8_t out[ESP_WIFI_MAX_SVC_SSI_LEN], uint16_t *length);
bool esp32_mquickjs_wifi_nan_ndp_service_busy(uint8_t service_id);
bool esp32_mquickjs_wifi_nan_ndp_peer_busy(const uint8_t peer[6]);
uint32_t esp32_mquickjs_wifi_nan_ndp_request_enter(const void *request);
void esp32_mquickjs_wifi_nan_ndp_request_leave(uint32_t identity, int error);
void esp32_mquickjs_wifi_nan_ndp_submitted(uint32_t identity, esp_err_t error);
esp_err_t esp32_mquickjs_wifi_nan_ndp_native_claim(void *ndl, uint8_t id, uint32_t *identity);
void esp32_mquickjs_wifi_nan_ndp_native_bound(uint32_t identity, bool bound);
uint32_t esp32_mquickjs_wifi_nan_ndp_deleting(const void *ndl);
void esp32_mquickjs_wifi_nan_ndp_deleted(uint32_t identity);
bool esp32_mquickjs_wifi_nan_ndp_peer_service(const uint8_t peer[6], uint8_t publisher_id,
    uint8_t service_id);
void esp32_mquickjs_wifi_nan_ndp_notice(const esp32_mquickjs_wifi_nan_sdk_notice_t *notice);
esp_err_t esp32_mquickjs_wifi_nan_ndp_mutation(uint32_t identity, bool end,
    esp32_mquickjs_wifi_nan_ndp_status_t *status);
void esp32_mquickjs_wifi_nan_ndp_mutation_done(uint32_t identity, bool end, esp_err_t error);
/* Native Wi-Fi task only. Prepare retires this peer's timers; commit also
 * removes the exact record, after the SDK has cleared its host preclaim. */
esp_err_t esp32_mquickjs_wifi_nan_ndp_retire_native(uint32_t identity, bool commit,
    esp32_mquickjs_wifi_nan_ndp_status_t *status);
esp_err_t esp32_mquickjs_wifi_nan_ndp_select_native_id(uint8_t current, uint8_t *selected);
/* These do not wait on shared SDK event bits or release timed-out storage.
 * Radio must serialize them with Session/service mutations. */
esp_err_t esp32_mquickjs_wifi_nan_sdk_ndp_request(uint8_t service_id,
    wifi_nan_datapath_req_t *request, uint32_t *identity);
esp_err_t esp32_mquickjs_wifi_nan_sdk_ndp_response(uint32_t identity, bool accept,
    const uint8_t *ssi, uint16_t length);
esp_err_t esp32_mquickjs_wifi_nan_sdk_ndp_end(uint32_t identity);
esp_err_t esp32_mquickjs_wifi_nan_sdk_ndp_release(uint32_t identity,
    esp32_mquickjs_wifi_nan_ndp_status_t *status);
esp_err_t esp32_mquickjs_wifi_nan_sdk_ndp_release_native(uint32_t identity,
    esp32_mquickjs_wifi_nan_ndp_status_t *status);
#endif
