#include "esp32_mquickjs_wifi_twt_probe_wake.h"
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI && CONFIG_SOC_WIFI_HE_SUPPORT && CONFIG_IDF_TARGET_ESP32C5
#include "esp_attr.h"
#include "freertos/FreeRTOS.h"
#include <stddef.h>

void pm_wake_up(void);
void pm_wake_done(void);
static DRAM_ATTR esp32_mquickjs_wifi_twt_probe_wake_snapshot_t s_probe_wake;
static DRAM_ATTR portMUX_TYPE s_probe_wake_lock = portMUX_INITIALIZER_UNLOCKED;

void esp32_mquickjs_wifi_twt_probe_wake_up_native(void)
{
    portENTER_CRITICAL_SAFE(&s_probe_wake_lock);
    bool available = s_probe_wake.fault == ESP_OK && !s_probe_wake.held &&
        !s_probe_wake.acquiring && !s_probe_wake.releasing;
    if (available) {
        s_probe_wake.held = true;
        s_probe_wake.acquiring = true;
    } else if (s_probe_wake.fault == ESP_OK) s_probe_wake.fault = ESP_ERR_INVALID_STATE;
    portEXIT_CRITICAL_SAFE(&s_probe_wake_lock);
    if (!available) return;
    /* Native submit admission already excludes another probe. Busy state
     * prevents snapshot/cleanup from mistaking this call for a drained ref. */
    pm_wake_up();
    portENTER_CRITICAL_SAFE(&s_probe_wake_lock);
    s_probe_wake.acquiring = false;
    portEXIT_CRITICAL_SAFE(&s_probe_wake_lock);
}
void esp32_mquickjs_wifi_twt_probe_wake_done_native(void)
{
    portENTER_CRITICAL_SAFE(&s_probe_wake_lock);
    if (s_probe_wake.acquiring || s_probe_wake.releasing) {
        if (s_probe_wake.fault == ESP_OK) s_probe_wake.fault = ESP_ERR_INVALID_STATE;
    }
    bool release = s_probe_wake.fault == ESP_OK && s_probe_wake.held;
    if (release) {
        s_probe_wake.held = false;
        s_probe_wake.releasing = true;
    }
    portEXIT_CRITICAL_SAFE(&s_probe_wake_lock);
    if (!release) return;
    pm_wake_done();
    portENTER_CRITICAL_SAFE(&s_probe_wake_lock);
    s_probe_wake.releasing = false;
    portEXIT_CRITICAL_SAFE(&s_probe_wake_lock);
}
void esp32_mquickjs_wifi_twt_probe_wake_snapshot(esp32_mquickjs_wifi_twt_probe_wake_snapshot_t *out)
{
    if (out == NULL) return;
    portENTER_CRITICAL_SAFE(&s_probe_wake_lock);
    *out = s_probe_wake;
    portEXIT_CRITICAL_SAFE(&s_probe_wake_lock);
}
#endif
