#pragma once
#include "sdkconfig.h"
#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI && (CONFIG_ESP_WIFI_NAN_SYNC_ENABLE || CONFIG_ESP_WIFI_NAN_USD_ENABLE)
#define ESP32_MQUICKJS_NAN_TX_CAPACITY 32U
typedef struct {
    uint32_t identity;
    uint64_t ticket;
    int64_t completed_us, retired_us;
    bool entered, allocated, tx_done, tx_succeeded, buffer_retired;
    /* USD retains its Action enum; synchronous NAN currently exposes only bool. */
    bool has_native_status;
    int native_status;
} esp32_mquickjs_wifi_nan_message_tx_status_t;
#if CONFIG_ESP_WIFI_NAN_SYNC_ENABLE
/* One framework follow-up at a time. context is a retained native output word,
 * never a JS address; the SDK ioctl passes this exact pointer to its WiFi task. */
esp_err_t esp32_mquickjs_wifi_nan_tx_message_begin(uint32_t identity, uint32_t *context);
bool esp32_mquickjs_wifi_nan_tx_message_status(uint32_t identity,
    esp32_mquickjs_wifi_nan_message_tx_status_t *status);
esp_err_t esp32_mquickjs_wifi_nan_tx_message_release(uint32_t identity);
#if CONFIG_ESP_WIFI_NAN_PAIRING
/* Wi-Fi-task scopes around the synchronous native producer. Both auth and
 * follow-up EBs use the existing pool; producer return is not TX retirement. */
esp_err_t esp32_mquickjs_wifi_nan_tx_pairing_enter(uint32_t identity, uint8_t service_id, bool authentication);
void esp32_mquickjs_wifi_nan_tx_pairing_leave(uint32_t identity);
bool esp32_mquickjs_wifi_nan_tx_pairing_drained(uint32_t identity);
/* Read terminal follow-up result separately from mere EB retirement. */
bool esp32_mquickjs_wifi_nan_tx_pairing_settled(uint32_t identity, esp_err_t *error, int64_t *completed_us);
#endif
uint32_t esp32_mquickjs_wifi_nan_tx_callback_enter(void *buffer);
/* NDP callback authority belongs to the captured operation, not its reusable
 * peer address. False still requires callback_leave for an entered ticket. */
bool esp32_mquickjs_wifi_nan_tx_callback_allowed(uint32_t ticket);
void esp32_mquickjs_wifi_nan_tx_callback_leave(uint32_t ticket, bool success);
typedef struct {
    uint32_t tracked, unidentified, submissions, rejected;
    size_t reserved_bytes;
    esp_err_t error;
    bool closing, identity_exhausted;
} esp32_mquickjs_wifi_nan_tx_status_t;
/* Radio mutation worker owns open/seal/close. Close succeeds only after every
 * actual native recycler has returned; it never forces SDK buffers free. */
esp_err_t esp32_mquickjs_wifi_nan_tx_open(void);
void esp32_mquickjs_wifi_nan_tx_seal(void);
esp_err_t esp32_mquickjs_wifi_nan_tx_close(void);
void esp32_mquickjs_wifi_nan_tx_status(esp32_mquickjs_wifi_nan_tx_status_t *status);
/* Caller must already have cancelled the exact native service and fenced its
 * callbacks. Unidentified queued frames conservatively delay reuse. */
bool esp32_mquickjs_wifi_nan_tx_service_drained(uint8_t service_id);
/* Only frame/callback retirement. Native NDL, RX, timers and security state
 * must be retired separately before reusing a datapath identity or peer. */
bool esp32_mquickjs_wifi_nan_tx_datapath_drained(uint8_t ndp_id, const uint8_t peer[6]);
void esp32_mquickjs_wifi_nan_tx_submitting(void *buffer);
/* Hooks shared with TWT/DPP's recycler. No buffer access after the real free. */
uint32_t esp32_mquickjs_wifi_nan_tx_recycling(void *buffer);
void esp32_mquickjs_wifi_nan_tx_recycled(uint32_t ticket);
#endif
#endif
