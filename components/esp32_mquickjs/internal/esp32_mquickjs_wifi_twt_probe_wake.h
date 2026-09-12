#pragma once
#include "sdkconfig.h"
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI && CONFIG_SOC_WIFI_HE_SUPPORT && CONFIG_IDF_TARGET_ESP32C5
#include "esp_err.h"
#include <stdbool.h>
typedef struct {
    esp_err_t fault;
    bool held, acquiring, releasing;
} esp32_mquickjs_wifi_twt_probe_wake_snapshot_t;
/* Native Wi-Fi task only. The build-local archive retargets just the probe's
 * one acquire and five release call sites. Other SDK users still call the
 * original shared PM functions. Timer presence is never ownership evidence.
 * Repeated release is harmless; unexpected acquire/reentrancy quarantines the
 * reference instead of decreasing another operation's shared PM count. */
void esp32_mquickjs_wifi_twt_probe_wake_up_native(void);
void esp32_mquickjs_wifi_twt_probe_wake_done_native(void);
void esp32_mquickjs_wifi_twt_probe_wake_snapshot(esp32_mquickjs_wifi_twt_probe_wake_snapshot_t *out);
#endif
