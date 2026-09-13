#pragma once
#include "sdkconfig.h"
#include "esp_err.h"
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI
/* Called from runtime-owned Session creation, before registry publication. */
esp_err_t esp32_mquickjs_wifi_raw_tx_pump_init(void);
/* No JS/runtime/Session pointer retained; safe from the Wi-Fi callback task. */
void esp32_mquickjs_wifi_raw_tx_pump_wake(void);
#endif
