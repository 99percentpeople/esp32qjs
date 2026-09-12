#pragma once
#include "sdkconfig.h"
#include <stdbool.h>
#include <stdint.h>
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI && CONFIG_ESP_WIFI_NAN_SYNC_ENABLE
/* Native Wi-Fi task only. True means consumed, including failed operations;
 * never fall back to a borrowed-address timer after an ownership failure. */
bool esp32_mquickjs_wifi_nan_timer_setfn(void *timer, void *callback, void *argument);
bool esp32_mquickjs_wifi_nan_timer_disarm(void *timer);
bool esp32_mquickjs_wifi_nan_timer_done(void *timer);
bool esp32_mquickjs_wifi_nan_timer_arm(void *timer, uint64_t us, bool repeat);
void esp32_mquickjs_wifi_nan_timer_peer_delete(const void *ndl, bool entering);
void esp32_mquickjs_wifi_nan_sdk_timer_process(uint8_t phase, void *argument);
#endif
