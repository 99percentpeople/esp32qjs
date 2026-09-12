#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

/* Private fixed-SDK result ownership. Every operation except held() runs on
 * the Wi-Fi task. A retained result is not proof of SDK callback retirement. */
typedef struct {
    uint32_t identity;
    esp_err_t error;
    esp_err_t cleanup_error;
    uint32_t callback_depth;
    int32_t event_id, failure_reason;
    uint8_t peer[6];
    bool sdk_attached, retained, terminal, closing, pin_available;
    uint8_t cleanup_stage;
    bool close_prepared, tracking_fault;
    bool enabled, started, sdk_state_retired;
    uint32_t reserved_bytes;
} esp32_mquickjs_wifi_wps_ap_result_status_t;

bool esp32qjs_wps_ap_result_held(void);
esp_err_t esp32qjs_wps_ap_result_reserve(uint32_t *identity);
/* Only a reservation that has never been attached to SDK state can be
 * discarded here. Bound results require the forthcoming native drain. */
esp_err_t esp32qjs_wps_ap_result_discard_unbound(uint32_t identity);
esp_err_t esp32qjs_wps_ap_result_status(uint32_t identity,
    esp32_mquickjs_wifi_wps_ap_result_status_t *status);
esp_err_t esp32qjs_wps_ap_result_pin_copy(uint32_t identity, uint8_t pin[8]);
esp_err_t esp32qjs_wps_ap_result_pin_commit(uint32_t identity);
esp_err_t esp32qjs_wps_ap_result_close_intent(uint32_t identity);

/* SDK integration, not general framework lifecycle entry points. */
bool esp32qjs_wps_ap_result_can_bind(void);
esp_err_t esp32qjs_wps_ap_result_bind(void *context);
void esp32qjs_wps_ap_result_detach(void *context, esp_err_t error);
void esp32qjs_wps_ap_result_cleanup_error(void *context, esp_err_t error);
uint32_t esp32qjs_wps_ap_result_identity(const void *context);
void *esp32qjs_wps_ap_result_context(uint32_t identity);
/* Native EAP may finish its protocol after the first public result, but never
 * after close intent. This entry also pins the AP against synchronous close. */
void *esp32qjs_wps_ap_result_native_context(uint32_t identity);
void *esp32qjs_wps_ap_result_activity_enter(uint32_t identity);
bool esp32qjs_wps_ap_result_busy(const void *context);
esp_err_t esp32qjs_wps_ap_result_peer_error(uint32_t identity, esp_err_t error,
    const uint8_t peer[6]);
void *esp32qjs_wps_ap_result_callback_enter(uint32_t identity);
void esp32qjs_wps_ap_result_callback_leave(uint32_t identity);
/* Stops admission and removes WPS advertisements. This is only a prefix:
 * EAP/native queue retirement must precede managed heap/result release. */
esp_err_t esp32qjs_wps_ap_result_prepare_close(void *context);
bool esp32qjs_wps_ap_result_deinit_allowed(void *context);
enum {
    ESP32QJS_WPS_AP_CLOSE_TYPE,
    ESP32QJS_WPS_AP_CLOSE_STATUS,
    ESP32QJS_WPS_AP_CLOSE_REENABLE_TIMER,
    ESP32QJS_WPS_AP_CLOSE_PIN_TIMER,
    ESP32QJS_WPS_AP_CLOSE_REGISTRAR_TIMERS,
    ESP32QJS_WPS_AP_CLOSE_EAP_TIMERS,
    ESP32QJS_WPS_AP_CLOSE_PEER_DELAYS,
    ESP32QJS_WPS_AP_CLOSE_BEACON_IE,
    ESP32QJS_WPS_AP_CLOSE_PROBE_IE,
    ESP32QJS_WPS_AP_CLOSE_PREPARED
};
/* Build-local hostapd boundary; executes exactly one checked cleanup step. */
esp_err_t esp32qjs_wps_ap_cleanup_step(void *context, unsigned stage);
esp_err_t esp32qjs_wps_ap_eapol_stop(void *context, uint32_t identity);
void esp32qjs_wps_ap_eapol_stop_peer(void *state);
/* Caller holds the SDK station-table lock. Publish EAP ownership transfer
 * before unlinking a peer, including deletion from the WPA3 task. */
void esp32qjs_wps_ap_eapol_detach_peer(void *peer);
esp_err_t esp32qjs_wps_ap_eapol_drain(void *context, uint32_t identity);
bool esp32qjs_wps_ap_eapol_drained(uint32_t identity);
esp_err_t esp32qjs_wps_ap_hostap_release(void *context);
esp_err_t esp32qjs_wps_ap_peer_remove_locked(void *context, void *peer);
esp_err_t esp32qjs_wps_ap_peer_delays_stop(void *context, uint32_t identity);
void esp32qjs_wps_ap_peer_release(void *context, void *peer);
/* Caller owns the peer semaphore and AP activity reference. */
void esp32qjs_wps_ap_receive_pinned(void *context, void *peer, const uint8_t *data, size_t size);
esp_err_t esp32qjs_wps_ap_result_event(void *context, int32_t event_id,
    const void *data, size_t size);
