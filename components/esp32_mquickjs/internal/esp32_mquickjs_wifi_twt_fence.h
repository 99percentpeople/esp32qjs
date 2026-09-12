#pragma once
#include "esp32_mquickjs_wifi_twt_lane.h"
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI && CONFIG_SOC_WIFI_HE_SUPPORT
#include "esp_timer.h"
#include <stdatomic.h>

/* Native owner storage, initially zero, serialized by its owner. Keep this
 * address alive until clear succeeds, including after public timeout/close.
 * The callback touches only reached. No JS/runtime pointer, task notification,
 * observation queue, global instance, or dynamically allocated wrapper state. */
typedef struct {
    esp32_mquickjs_wifi_twt_token_t token;
    uint32_t revision;
    esp_timer_handle_t timer;
    atomic_bool reached;
    esp_err_t start_error, cleanup_error;
    const char *stage;
    bool started, stopped, ready, clearing;
} esp32_mquickjs_wifi_twt_fence_t;

/* Arms a fresh ESP_TIMER_TASK marker. ESP_OK means armed, NOT fence complete.
 * A start/create error still owns this token until clear; no implicit retry.
 * SDK calls below are outside critical sections; poll/clear use a one-tick
 * callback-exit wait budget, not a wall-clock scheduling guarantee. */
esp_err_t esp32_mquickjs_wifi_twt_fence_begin(esp32_mquickjs_wifi_twt_fence_t *fence,
    const esp32_mquickjs_wifi_twt_token_t *token, uint32_t revision);
/* ESP_ERR_NOT_FINISHED while the marker has not run. Then confirms its callback
 * exit, queues timer deletion and fences the native Wi-Fi ioctl queue. Errors
 * retain storage and retry only the unfinished suffix on the next poll.
 * ESP_OK attests this ORDERING POINT ONLY, never absence of TWT timers/TX/work.
 * Owner must independently establish native quiescence and recheck the live
 * lane's exact token/revision after this call before consuming any proof. */
esp_err_t esp32_mquickjs_wifi_twt_fence_poll(esp32_mquickjs_wifi_twt_fence_t *fence,
    const esp32_mquickjs_wifi_twt_token_t *token, uint32_t revision);
/* Cancels proof delivery and retires callback storage. Success resets the
 * record for reuse; it does not claim the marker ever ran or Wi-Fi is drained.
 * A failed clear forbids poll and preserves the timer until a later clear.
 * esp_timer_delete acceptance can precede the SDK allocator's eventual free. */
esp_err_t esp32_mquickjs_wifi_twt_fence_clear(esp32_mquickjs_wifi_twt_fence_t *fence,
    const esp32_mquickjs_wifi_twt_token_t *token, uint32_t revision);
#endif
