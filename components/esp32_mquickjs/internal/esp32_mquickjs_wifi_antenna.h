#pragma once
#include "sdkconfig.h"
#include "esp_err.h"

#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI
#include "esp32_mquickjs_wifi_radio.h"

/* Runtime task, with the Radio mutation mutex held and every Radio owner gone.
 * This adds the native PHY access lock and the BLE/controller admission check.
 * Configuration and GPIO ownership survive Wi-Fi deinit and runtime restart. */
esp_err_t esp32_mquickjs_wifi_antenna_write(bool gpio,
    const esp32_mquickjs_wifi_antenna_snapshot_t *requested,
    esp32_mquickjs_wifi_radio_config_result_t *result);
/* Failed physical rollback is boot-scoped: restarting Wi-Fi or JS cannot
 * restore shared PHY/GPIO state. Both Wi-Fi initialization and BLE open check it. */
esp_err_t esp32_mquickjs_wifi_antenna_fault(void);
bool esp32_mquickjs_wifi_antenna_valid(const esp_phy_ant_config_t *config);
#endif
