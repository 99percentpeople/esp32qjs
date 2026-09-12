#pragma once
#include "sdkconfig.h"
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI && CONFIG_ESP_WIFI_NAN_PAIRING
#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint32_t identity;
    uint8_t peer[6];
    esp_err_t error;
    bool active, authenticated, paired, closing, retired, traffic_pending;
    int64_t completed_us;
} esp32_mquickjs_wifi_nan_pasn_status_t;

/* Non-secret metadata for the two SDK-owned RAM credential slots. IDs are
 * boot-unique and change whenever completed pairing replaces key material. */
#define ESP32_MQUICKJS_NAN_CACHED_PAIRINGS 2U
typedef struct { uint32_t identity; uint8_t peer[6]; int64_t expires_us; } esp32_mquickjs_wifi_nan_credential_info_t;
typedef struct {
    unsigned count;
    esp32_mquickjs_wifi_nan_credential_info_t entries[ESP32_MQUICKJS_NAN_CACHED_PAIRINGS];
} esp32_mquickjs_wifi_nan_credentials_t;
esp_err_t esp32_mquickjs_wifi_nan_pairing_credentials(uint8_t service_id,
    esp32_mquickjs_wifi_nan_credentials_t *out);
esp_err_t esp32_mquickjs_wifi_nan_pairing_commit(uint32_t identity, uint32_t *credential_id);
bool esp32_mquickjs_wifi_nan_credential_info(unsigned slot, esp32_mquickjs_wifi_nan_credential_info_t *out);
esp_err_t esp32_mquickjs_wifi_nan_credential_replace(unsigned slot, const uint8_t peer[6], int64_t expires_us, uint32_t *identity);

/* Exact service selection is mandatory. Verification selects one credential
 * from this Session's native cache; authentication uses an explicit PIN and
 * credential_id == 0. This is an internal command, not a JS contract. */
typedef struct {
    uint32_t service_identity;
    uint32_t pincode;
    uint32_t credential_id;
    uint8_t service_id, peer_service_id, peer[6];
    bool initiator, verification;
} esp32_mquickjs_wifi_nan_pairing_config_t;
esp_err_t esp32_mquickjs_wifi_nan_pairing_start(
    const esp32_mquickjs_wifi_nan_pairing_config_t *config,
    esp32_mquickjs_wifi_nan_pasn_status_t *status);
/* Radio service cancellation checks before taking NAN_DATA_LOCK. */
esp_err_t esp32_mquickjs_wifi_nan_pairing_service_idle(uint8_t service_id);
/* Wi-Fi-task-only admission, including the PASN initialization call itself. */
bool esp32_mquickjs_wifi_nan_pairing_services(const uint8_t peer[6],
    uint8_t *service_id, uint8_t *peer_service_id);
void esp32_mquickjs_wifi_nan_pairing_release_binding(void);
/* Sidecar service ownership uses the existing Session TX/control allocation;
 * native paired-peer cache layout and crypto records remain SDK-owned. */
uint8_t esp32_mquickjs_wifi_nan_pairing_cache_service(unsigned slot);
bool esp32_mquickjs_wifi_nan_pairing_cache_bind(unsigned slot, uint8_t service_id);
struct nan_paired_peer;
/* Called by native security consumers with NAN_DATA_LOCK held. */
const struct nan_paired_peer *esp32_mquickjs_wifi_nan_pairing_cached_peer(
    uint8_t service_id, const uint8_t peer[6]);

/* Native Radio worker only. Commands execute on the Wi-Fi task; no caller
 * waits while holding NAN_DATA_LOCK. Snapshot contains no credentials.
 * PASN retirement does NOT prove pairing follow-up, driver TX or RF retirement.
 * The parent Radio/Session must retain those independent owners. */
esp_err_t esp32_mquickjs_wifi_nan_pasn_status(esp32_mquickjs_wifi_nan_pasn_status_t *status);
esp_err_t esp32_mquickjs_wifi_nan_pasn_close(uint32_t identity);
/* Exclusive parent Radio close, before physical STOP or host service reset. */
esp_err_t esp32_mquickjs_wifi_nan_pasn_shutdown(void);
esp_err_t esp32_mquickjs_wifi_nan_pasn_verify_responder(const uint8_t peer[6]);
/* Build-local native callbacks only, on the Wi-Fi task. A null peer queries
 * current admission; a nonnull peer must exactly match the authorized peer. */
uint32_t esp32_mquickjs_wifi_nan_pasn_identity(const uint8_t *peer);
void esp32_mquickjs_wifi_nan_pasn_result(const uint8_t *peer, esp_err_t error, bool paired);
esp_err_t esp32_mquickjs_wifi_nan_pairing_cancel_pending(void);
bool esp32_mquickjs_wifi_nan_pairing_followup_pending(void);
#endif
