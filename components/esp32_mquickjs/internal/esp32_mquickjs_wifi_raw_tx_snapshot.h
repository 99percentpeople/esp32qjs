#pragma once
#include "sdkconfig.h"
#include "esp32_mquickjs_wifi_raw_tx_validate.h"

typedef enum {
    ESP32_MQUICKJS_WIFI_RAW_TX_DRIVER_UNKNOWN,
    ESP32_MQUICKJS_WIFI_RAW_TX_DRIVER_SUCCESS,
    ESP32_MQUICKJS_WIFI_RAW_TX_DRIVER_FAILED,
} esp32_mquickjs_wifi_raw_tx_driver_status_t;
typedef struct {
    uint64_t callback_time_us;
    esp32_mquickjs_wifi_raw_tx_interface_t interface;
    esp32_mquickjs_wifi_raw_tx_driver_status_t status;
    int32_t raw_status, raw_rate;
    uint8_t source[6], destination[6];
    bool source_available, destination_available;
    /* SDK uint8 body report only; NOT a MAC/frame length or readable pointer span. */
    uint8_t raw_body_length;
    /* TX-info completion byte from the pinned SDK descriptor, before the
     * wifi_tx_status_t collapse. This is not the LMAC state-machine byte.
     * Available only on the reviewed descriptor-identity path; preserve
     * unknown values without assigning them an unverified reason. */
    bool mac_status_available;
    uint8_t mac_status_byte;
} esp32_mquickjs_wifi_raw_tx_snapshot_t;

#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI
#include "esp_wifi.h"
/* Call only within SDK TX callback. Copies fixed MAC fields and scalars, never
 * tx_info->data. No stored SDK/JS pointer, allocation, driver call or RF ACK claim.
 * mac_status_byte is the LMAC descriptor completion byte, or -1 when unavailable.
 * This snapshot has no operation identity: the eventual broker must quarantine
 * a timed-out operation until native completion is resolved before lane reuse. */
bool esp32_mquickjs_wifi_raw_tx_snapshot(const esp_80211_tx_info_t *info,
    uint64_t callback_time_us, int mac_status_byte,
    esp32_mquickjs_wifi_raw_tx_snapshot_t *output);
/* Observation names for known LMAC completion bytes on the pinned C3/S3/C5
 * builds. Returns NULL for unknown values. Not an Espressif public contract. */
const char *esp32_mquickjs_wifi_raw_tx_mac_status_name(uint8_t mac_status_byte);
#endif
