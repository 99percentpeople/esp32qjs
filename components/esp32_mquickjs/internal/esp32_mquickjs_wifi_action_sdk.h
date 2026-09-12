#pragma once
#include "sdkconfig.h"
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI
#include "esp_wifi.h"
/* Unique rx_cb identity used by all framework Action/ROC requests, including
 * cancel. The initial API receives no Action RX stream; Monitor is separate.
 * Never use NULL or another module's callback: the SDK event context is rx_cb. */
int esp32_mquickjs_wifi_action_receive(uint8_t *header, uint8_t *payload, size_t length, uint8_t channel);
/* Synchronous no-op through the reviewed native ioctl execution queue. */
esp_err_t esp32_mquickjs_wifi_action_sdk_fence(void);
/* Runs in the same native ioctl queue. ESP_OK attests the complete reviewed
 * off-channel record is zero; ESP_ERR_TIMEOUT means native ownership remains.
 * This is not an event-loop fence and does not authorize releasing a JS token. */
esp_err_t esp32_mquickjs_wifi_action_sdk_quiescent(void);
/* Fixed C5 only: read saved PHY policy for both bands in one native ioctl task
 * execution, including a band hidden by the public getter's current-mode filter.
 * No START/mode change or global write. Output is committed only on success.
 * The caller owns Radio admission and validates the returned protocol/widths. */
typedef struct {
    wifi_protocols_t protocols;
    wifi_bandwidths_t bandwidths;
} esp32_mquickjs_wifi_saved_phy_t;
esp_err_t esp32_mquickjs_wifi_action_sdk_saved_phy(wifi_interface_t interface,
    esp32_mquickjs_wifi_saved_phy_t *output);
#endif
