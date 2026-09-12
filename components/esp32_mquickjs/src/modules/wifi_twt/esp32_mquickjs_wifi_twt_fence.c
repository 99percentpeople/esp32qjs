#include "esp32_mquickjs_wifi_twt_fence.h"
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI && CONFIG_SOC_WIFI_HE_SUPPORT
#include "esp32_mquickjs_wifi_action_sdk.h"
#include <string.h>

static bool twt_fence_exact(const esp32_mquickjs_wifi_twt_fence_t *fence,
    const esp32_mquickjs_wifi_twt_token_t *token, uint32_t revision)
{
    return fence != NULL && token != NULL && token->identity != 0U && token->generation != 0U &&
        fence->token.identity == token->identity && fence->token.generation == token->generation &&
        fence->revision == revision;
}
static void twt_fence_callback(void *opaque)
{
    esp32_mquickjs_wifi_twt_fence_t *fence = opaque;
    /* TASK callbacks are serialized: all earlier running TASK callbacks have
     * returned. Do not take an owner/Radio lock or access any other field. */
    atomic_store_explicit(&fence->reached, true, memory_order_release);
}
esp_err_t esp32_mquickjs_wifi_twt_fence_begin(esp32_mquickjs_wifi_twt_fence_t *fence,
    const esp32_mquickjs_wifi_twt_token_t *token, uint32_t revision)
{
    if (fence == NULL || token == NULL || token->identity == 0U || token->generation == 0U ||
        revision == UINT32_MAX) return ESP_ERR_INVALID_ARG;
    if (fence->token.identity != 0U || fence->timer != NULL) return ESP_ERR_INVALID_STATE;
    esp32_mquickjs_wifi_twt_token_t identity = *token;
    memset(fence, 0, sizeof(*fence));
    fence->token = identity;
    fence->revision = revision;
    atomic_init(&fence->reached, false);
    const esp_timer_create_args_t args = {.callback = twt_fence_callback, .arg = fence,
        .dispatch_method = ESP_TIMER_TASK, .name = "qjs_twt_fence"};
    fence->stage = "twt-timer-create";
    fence->start_error = esp_timer_create(&args, &fence->timer);
    if (fence->start_error != ESP_OK) return fence->start_error;
    fence->stage = "twt-timer-start";
    fence->start_error = esp_timer_start_once(fence->timer, 1);
    if (fence->start_error != ESP_OK) return fence->start_error;
    fence->started = true;
    fence->stage = "twt-timer-wait";
    return ESP_OK;
}

static esp_err_t twt_fence_retire_timer(esp32_mquickjs_wifi_twt_fence_t *fence)
{
    if (fence->timer == NULL) return ESP_OK;
    if (!fence->stopped) {
        fence->stage = "twt-timer-stop";
        /* reached is set before callback RETURN. In contrast to static boot
         * storage, this record may later be freed: require actual callback exit
         * before deleting/rebinding the timer's opaque argument. */
        fence->cleanup_error = esp_timer_stop_blocking(fence->timer, 1);
        if (fence->cleanup_error != ESP_OK) return fence->cleanup_error;
        fence->stopped = true;
    }
    fence->stage = "twt-timer-delete";
    fence->cleanup_error = esp_timer_delete(fence->timer);
    if (fence->cleanup_error != ESP_OK) return fence->cleanup_error;
    fence->timer = NULL;
    return ESP_OK;
}
esp_err_t esp32_mquickjs_wifi_twt_fence_poll(esp32_mquickjs_wifi_twt_fence_t *fence,
    const esp32_mquickjs_wifi_twt_token_t *token, uint32_t revision)
{
    if (!twt_fence_exact(fence, token, revision) || fence->clearing) return ESP_ERR_INVALID_STATE;
    if (fence->start_error != ESP_OK) return fence->start_error;
    if (!fence->started) return ESP_ERR_INVALID_STATE;
    if (fence->ready) return ESP_OK;
    if (!atomic_load_explicit(&fence->reached, memory_order_acquire)) return ESP_ERR_NOT_FINISHED;
    esp_err_t error = twt_fence_retire_timer(fence);
    if (error != ESP_OK) return error;
    /* The callback may have queued work for the native Wi-Fi task. Queue a new
     * native marker AFTER timer callback exit, preserving this order on retry. */
    fence->stage = "twt-native-fence";
    fence->cleanup_error = esp32_mquickjs_wifi_action_sdk_fence();
    if (fence->cleanup_error != ESP_OK) return fence->cleanup_error;
    fence->ready = true;
    fence->stage = NULL;
    return ESP_OK;
}
esp_err_t esp32_mquickjs_wifi_twt_fence_clear(esp32_mquickjs_wifi_twt_fence_t *fence,
    const esp32_mquickjs_wifi_twt_token_t *token, uint32_t revision)
{
    if (!twt_fence_exact(fence, token, revision)) return ESP_ERR_INVALID_STATE;
    fence->clearing = true;
    fence->ready = false;
    esp_err_t error = twt_fence_retire_timer(fence);
    if (error != ESP_OK) return error;
    /* There is no callback left that can touch this storage. SDK deletion may
     * free its own timer object later, but does not dereference this arg again. */
    memset(fence, 0, sizeof(*fence));
    atomic_init(&fence->reached, false);
    return ESP_OK;
}
#endif
