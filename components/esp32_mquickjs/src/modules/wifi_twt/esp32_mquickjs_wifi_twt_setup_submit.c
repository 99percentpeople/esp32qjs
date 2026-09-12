#include "esp32_mquickjs_wifi_twt_setup_submit.h"
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI && CONFIG_SOC_WIFI_HE_SUPPORT && CONFIG_IDF_TARGET_ESP32C5
#include "esp32_mquickjs_wifi_twt_options.h"
#include "esp32_mquickjs_wifi_twt_setup_result.h"
#include "esp_attr.h"
#include "esp_wifi_he.h"
#include "freertos/FreeRTOS.h"
#include <string.h>

/* One synchronous public API call at a time, independent of the eight result
 * records/agreements. SDK receives only this boot-stable native configuration.
 * An impossible/uncertain return retains it and rejects overwrite; it does
 * not pretend runtime restart establishes SDK retirement. */
static DRAM_ATTR struct {
    esp32_mquickjs_wifi_twt_setup_dispatch_t result;
    bool occupied;
} s_setup_submit;
static DRAM_ATTR portMUX_TYPE s_setup_submit_lock = portMUX_INITIALIZER_UNLOCKED;
_Static_assert(sizeof(esp32_mquickjs_wifi_twt_setup_dispatch_t) == 36 && sizeof(s_setup_submit) == 40,
    "reviewed C5 setup dispatch storage");

esp_err_t esp32_mquickjs_wifi_twt_setup_submit_enter_native(const wifi_itwt_setup_config_t *config,
    uint32_t *identity)
{
    if (config == NULL || identity == NULL || *identity != 0U) return ESP_ERR_INVALID_ARG;
    portENTER_CRITICAL_SAFE(&s_setup_submit_lock);
    esp_err_t error = ESP_OK;
    if (config == &s_setup_submit.result.config) {
        if (!s_setup_submit.occupied || s_setup_submit.result.identity == 0U ||
            s_setup_submit.result.native_entered || s_setup_submit.result.handoff_error != ESP_OK)
            error = ESP_ERR_INVALID_STATE;
        else {
            s_setup_submit.result.native_entered = true;
            *identity = s_setup_submit.result.identity;
        }
    }
    portEXIT_CRITICAL_SAFE(&s_setup_submit_lock);
    return error;
}
bool esp32_mquickjs_wifi_twt_setup_submit_driver_native(const wifi_itwt_setup_config_t *config,
    uint32_t identity)
{
    portENTER_CRITICAL_SAFE(&s_setup_submit_lock);
    bool exact = config == &s_setup_submit.result.config && identity != 0U && s_setup_submit.occupied &&
        s_setup_submit.result.identity == identity && s_setup_submit.result.native_entered &&
        !s_setup_submit.result.driver_called && !s_setup_submit.result.native_completed &&
        s_setup_submit.result.handoff_error == ESP_OK;
    if (exact) s_setup_submit.result.driver_called = true;
    portEXIT_CRITICAL_SAFE(&s_setup_submit_lock);
    return exact;
}
void esp32_mquickjs_wifi_twt_setup_submit_complete_native(const wifi_itwt_setup_config_t *config,
    uint32_t identity, esp_err_t driver_error)
{
    portENTER_CRITICAL_SAFE(&s_setup_submit_lock);
    if (config == &s_setup_submit.result.config && identity != 0U && s_setup_submit.occupied &&
        s_setup_submit.result.identity == identity && s_setup_submit.result.native_entered) {
        if (s_setup_submit.result.driver_called) s_setup_submit.result.driver_error = driver_error;
        s_setup_submit.result.native_completed = true;
    }
    portEXIT_CRITICAL_SAFE(&s_setup_submit_lock);
}
esp_err_t esp32_mquickjs_wifi_twt_sdk_individual_submit(const wifi_itwt_setup_config_t *config,
    esp32_mquickjs_wifi_twt_setup_dispatch_t *output)
{
    if (config == NULL || output == NULL || output->identity != 0U) return ESP_ERR_INVALID_ARG;
    esp32_mquickjs_wifi_itwt_options_t options = {.config = *config, .timeout_ms = 1};
    if (!esp32_mquickjs_wifi_itwt_options_valid(&options)) return ESP_ERR_INVALID_ARG;
    portENTER_CRITICAL_SAFE(&s_setup_submit_lock);
    if (s_setup_submit.occupied) {
        portEXIT_CRITICAL_SAFE(&s_setup_submit_lock);
        return ESP_ERR_INVALID_STATE;
    }
    s_setup_submit.occupied = true;
    s_setup_submit.result = (esp32_mquickjs_wifi_twt_setup_dispatch_t){.config = options.config};
    portEXIT_CRITICAL_SAFE(&s_setup_submit_lock);
    uint32_t identity = 0;
    esp_err_t error = esp32_mquickjs_wifi_twt_setup_result_begin_native((int16_t)options.config.twt_id, &identity);
    if (error != ESP_OK) {
        portENTER_CRITICAL_SAFE(&s_setup_submit_lock);
        memset(&s_setup_submit, 0, sizeof(s_setup_submit));
        portEXIT_CRITICAL_SAFE(&s_setup_submit_lock);
        return error;
    }
    portENTER_CRITICAL_SAFE(&s_setup_submit_lock);
    s_setup_submit.result.identity = identity;
    portEXIT_CRITICAL_SAFE(&s_setup_submit_lock);
    /* Reviewed public API operation 110 retains its initialization/TWT/AP
     * checks and ioctl semaphore wait. Never call it recursively from the
     * Wi-Fi native wrapper and never pass a JS/Future-owned buffer. */
    error = esp_wifi_sta_itwt_setup(&s_setup_submit.result.config);
    portENTER_CRITICAL_SAFE(&s_setup_submit_lock);
    s_setup_submit.result.sdk_error = error;
    bool certain = s_setup_submit.result.native_entered ? s_setup_submit.result.native_completed : error != ESP_OK;
    if (!certain) s_setup_submit.result.handoff_error = ESP_ERR_INVALID_STATE;
    if (certain) *output = s_setup_submit.result;
    else {
        /* An unexpectedly early return may race native config writeback. Do
         * not read that buffer until native completion is independently known. */
        *output = (esp32_mquickjs_wifi_twt_setup_dispatch_t){.config = options.config,
            .identity = identity, .sdk_error = error, .handoff_error = ESP_ERR_INVALID_STATE,
            .native_entered = s_setup_submit.result.native_entered,
            .driver_called = s_setup_submit.result.driver_called,
            .native_completed = s_setup_submit.result.native_completed};
    }
    portEXIT_CRITICAL_SAFE(&s_setup_submit_lock);
    if (!certain) return ESP_ERR_INVALID_STATE;
    /* Native control events can arrive before this public API returns. Keep
     * submitting true until that synchronous return, independently of events. */
    esp32_mquickjs_wifi_twt_setup_result_submitted_native(identity, error);
    portENTER_CRITICAL_SAFE(&s_setup_submit_lock);
    memset(&s_setup_submit, 0, sizeof(s_setup_submit));
    portEXIT_CRITICAL_SAFE(&s_setup_submit_lock);
    return error;
}
#endif
