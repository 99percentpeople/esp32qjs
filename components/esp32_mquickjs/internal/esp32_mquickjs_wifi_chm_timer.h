#pragma once
#include "sdkconfig.h"
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI && (CONFIG_ESP_WIFI_DPP_SUPPORT || CONFIG_ESP_WIFI_NAN_USD_ENABLE)
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "rom/ets_sys.h"

/* Private dispatch for the two hash-gated native channel-manager timers.
 * All mutations/processing run on the native Wi-Fi task; snapshots are safe
 * from another task. The esp_timer callback is a generic deadline wake. */
bool esp32qjs_wifi_chm_timer_setfn(ETSTimer *timer, ETSTimerFunc *callback, void *argument);
bool esp32qjs_wifi_chm_timer_disarm(ETSTimer *timer);
bool esp32qjs_wifi_chm_timer_done(ETSTimer *timer);
bool esp32qjs_wifi_chm_timer_arm(ETSTimer *timer, uint64_t us, bool repeat);
void *esp32qjs_wifi_chm_record_reset(void *destination, int value, size_t size);
/* A retained DPP operation's existing native poll can deliver a due timeout
 * whose queue admission failed. Never runs an unexpired or revoked arm. */
esp_err_t esp32qjs_wifi_chm_timer_service_native(void);
typedef struct {
    esp_err_t error, cleanup_error;
    uint32_t last_identity, post_failures, ignored_messages;
    uint8_t timers_held, active_arms;
} esp32qjs_wifi_chm_timer_status_t;
void esp32qjs_wifi_chm_timer_status(esp32qjs_wifi_chm_timer_status_t *out);
#endif
