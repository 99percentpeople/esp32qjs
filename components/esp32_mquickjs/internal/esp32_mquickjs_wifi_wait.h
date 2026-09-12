#pragma once

#include "sdkconfig.h"
#if CONFIG_ESP32_MQUICKJS_WIFI_RADIO
#include <stdint.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/* A public stop call shares one cooperative wait budget across Radio and both
 * netifs. Only its task consumes this scope; callbacks and foreign tasks retain
 * their normal limits. No JS pointers. SDK calls/mutexes are not preemptible. */
esp_err_t esp32_mquickjs_wifi_wait_begin(uint32_t timeout_ms);
TickType_t esp32_mquickjs_wifi_wait_remaining(TickType_t fallback);
void esp32_mquickjs_wifi_wait_end(void);
#endif
