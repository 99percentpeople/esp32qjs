#pragma once
#include "esp32_mquickjs_wifi_smartconfig_events.h"
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI && CONFIG_ESP_NETIF_USES_TCPIP_WITH_BSD_API && CONFIG_LWIP_IPV4
#include "esp_attr.h"
#include "rom/ets_sys.h"

#define ESP32_MQUICKJS_SMARTCONFIG_TIMERS 9U
typedef enum {
    ESP32_MQUICKJS_SC_TIMER_NONE,
    ESP32_MQUICKJS_SC_TIMER_ADMISSION,
    ESP32_MQUICKJS_SC_TIMER_CREATE,
    ESP32_MQUICKJS_SC_TIMER_START,
    ESP32_MQUICKJS_SC_TIMER_STOP,
    ESP32_MQUICKJS_SC_TIMER_DELETE,
} esp32_mquickjs_wifi_smartconfig_timer_stage_t;
typedef struct {
    uint32_t handles, callbacks, busy, reserved_bytes;
    esp_err_t error, cleanup_error;
    esp32_mquickjs_wifi_smartconfig_timer_stage_t stage;
    bool closing;
} esp32_mquickjs_wifi_smartconfig_timer_status_t;

/* The caller provides the reviewed decoder's nine static ETSTimer addresses
 * before native start. No arbitrary driver/BT/TWT timer is claimed. Reserve
 * through cleanup; caller serializes decoder start/stop and owner release. */
esp_err_t esp32_mquickjs_wifi_smartconfig_timers_begin(
    const esp32_mquickjs_wifi_smartconfig_token_t *token,
    ETSTimer *const timers[ESP32_MQUICKJS_SMARTCONFIG_TIMERS]);
esp_err_t esp32_mquickjs_wifi_smartconfig_timers_status(
    const esp32_mquickjs_wifi_smartconfig_token_t *token,
    esp32_mquickjs_wifi_smartconfig_timer_status_t *status);
esp_err_t esp32_mquickjs_wifi_smartconfig_timers_close(
    const esp32_mquickjs_wifi_smartconfig_token_t *token);
/* Background worker only. Revokes callbacks before stop/delete, retains failed
 * suffixes. Success proves this timer registry drained, not queued native work
 * or decoder stop. A native queue fence and decoder shutdown are still needed. */
esp_err_t esp32_mquickjs_wifi_smartconfig_timers_cleanup(
    const esp32_mquickjs_wifi_smartconfig_token_t *token);
esp_err_t esp32_mquickjs_wifi_smartconfig_timers_release(
    const esp32_mquickjs_wifi_smartconfig_token_t *token);

/* Legacy SDK dispatch adapters: true means exactly this owner handles timer. */
bool esp32_mquickjs_wifi_smartconfig_timer_setfn(ETSTimer *timer, ETSTimerFunc *fn, void *argument);
bool esp32_mquickjs_wifi_smartconfig_timer_disarm(ETSTimer *timer);
bool esp32_mquickjs_wifi_smartconfig_timer_done(ETSTimer *timer);
bool esp32_mquickjs_wifi_smartconfig_timer_arm(ETSTimer *timer, uint64_t us, bool repeat);
#endif
