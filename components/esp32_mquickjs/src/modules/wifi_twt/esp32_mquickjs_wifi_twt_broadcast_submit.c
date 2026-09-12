#include "esp32_mquickjs_wifi_twt_broadcast_submit.h"
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI && CONFIG_SOC_WIFI_HE_SUPPORT && CONFIG_IDF_TARGET_ESP32C5
#include "esp32_mquickjs_wifi_twt_options.h"
#include "esp32_mquickjs_wifi_twt_broadcast_timer.h"
#include "esp_attr.h"
#include "esp_wifi_he.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static DRAM_ATTR struct {
    esp32_mquickjs_wifi_btwt_dispatch_t result;
    TaskHandle_t native_task;
    bool occupied;
} s_btwt_submit;
static DRAM_ATTR portMUX_TYPE s_btwt_submit_lock = portMUX_INITIALIZER_UNLOCKED;
_Static_assert(sizeof(esp32_mquickjs_wifi_btwt_dispatch_t) == 28 && sizeof(s_btwt_submit) == 36,
    "reviewed C5 broadcast dispatch storage");

esp_err_t esp32_mquickjs_wifi_btwt_submit_enter_native(const wifi_btwt_setup_config_t *config, bool *managed)
{
    if (config == NULL || managed == NULL || *managed) return ESP_ERR_INVALID_ARG;
    TaskHandle_t task = xTaskGetCurrentTaskHandle();
    portENTER_CRITICAL_SAFE(&s_btwt_submit_lock);
    esp_err_t error = ESP_OK;
    if (config == &s_btwt_submit.result.config) {
        if (!task || !s_btwt_submit.occupied || s_btwt_submit.result.native_entered ||
            s_btwt_submit.result.handoff_error != ESP_OK) error = ESP_ERR_INVALID_STATE;
        else {
            s_btwt_submit.result.native_entered = true;
            s_btwt_submit.native_task = task;
            *managed = true;
        }
    }
    portEXIT_CRITICAL_SAFE(&s_btwt_submit_lock);
    return error;
}
bool esp32_mquickjs_wifi_btwt_submit_driver_native(const wifi_btwt_setup_config_t *config)
{
    TaskHandle_t task = xTaskGetCurrentTaskHandle();
    portENTER_CRITICAL_SAFE(&s_btwt_submit_lock);
    bool exact = config == &s_btwt_submit.result.config && s_btwt_submit.occupied &&
        s_btwt_submit.native_task == task && s_btwt_submit.result.native_entered &&
        !s_btwt_submit.result.driver_called && !s_btwt_submit.result.native_completed &&
        s_btwt_submit.result.handoff_error == ESP_OK;
    if (exact) s_btwt_submit.result.driver_called = true;
    portEXIT_CRITICAL_SAFE(&s_btwt_submit_lock);
    return exact;
}
esp_err_t esp32_mquickjs_wifi_btwt_submit_bind_native(unsigned slot, uint32_t identity,
    const uint8_t parameter[17])
{
    if (slot >= 32 || !identity || parameter == NULL) return ESP_ERR_INVALID_ARG;
    TaskHandle_t task = xTaskGetCurrentTaskHandle();
    portENTER_CRITICAL_SAFE(&s_btwt_submit_lock);
    bool scoped = s_btwt_submit.occupied && s_btwt_submit.native_task == task &&
        s_btwt_submit.result.native_entered && !s_btwt_submit.result.native_completed;
    esp_err_t error = ESP_OK;
    if (scoped) {
        if (!s_btwt_submit.result.driver_called || s_btwt_submit.result.identity != 0 ||
            s_btwt_submit.result.handoff_error != ESP_OK ||
            s_btwt_submit.result.config.btwt_id != slot || (parameter[10] >> 3) != slot ||
            ((parameter[3] >> 1) & 7U) != (unsigned)s_btwt_submit.result.config.setup_cmd)
            error = ESP_ERR_INVALID_STATE;
        else s_btwt_submit.result.identity = identity;
        if (error != ESP_OK && s_btwt_submit.result.handoff_error == ESP_OK)
            s_btwt_submit.result.handoff_error = error;
    }
    portEXIT_CRITICAL_SAFE(&s_btwt_submit_lock);
    if (!scoped || error != ESP_OK) return error;
    /* Never nest the dispatch and timer locks. Config remains occupied until
     * the native process and public synchronous call both return. A premature
     * public return freezes storage, so even that error cannot lose this ID. */
    error = esp32_mquickjs_wifi_btwt_setup_hold_native(slot, identity);
    if (error != ESP_OK) {
        portENTER_CRITICAL_SAFE(&s_btwt_submit_lock);
        if (s_btwt_submit.result.handoff_error == ESP_OK) s_btwt_submit.result.handoff_error = error;
        portEXIT_CRITICAL_SAFE(&s_btwt_submit_lock);
    }
    return error;
}
void esp32_mquickjs_wifi_btwt_submit_complete_native(const wifi_btwt_setup_config_t *config, esp_err_t error)
{
    TaskHandle_t task = xTaskGetCurrentTaskHandle();
    portENTER_CRITICAL_SAFE(&s_btwt_submit_lock);
    if (config == &s_btwt_submit.result.config && s_btwt_submit.occupied &&
        s_btwt_submit.native_task == task && s_btwt_submit.result.native_entered &&
        !s_btwt_submit.result.native_completed) {
        if (s_btwt_submit.result.driver_called) s_btwt_submit.result.driver_error = error;
        s_btwt_submit.result.native_completed = true;
        s_btwt_submit.native_task = NULL;
    }
    portEXIT_CRITICAL_SAFE(&s_btwt_submit_lock);
}
esp_err_t esp32_mquickjs_wifi_twt_sdk_broadcast_submit(const wifi_btwt_setup_config_t *config,
    esp32_mquickjs_wifi_btwt_dispatch_t *output)
{
    if (config == NULL || output == NULL || output->identity != 0) return ESP_ERR_INVALID_ARG;
    esp32_mquickjs_wifi_btwt_options_t options = {.config = *config, .timeout_ms = 1};
    if (!esp32_mquickjs_wifi_btwt_options_valid(&options)) return ESP_ERR_INVALID_ARG;
    portENTER_CRITICAL_SAFE(&s_btwt_submit_lock);
    if (s_btwt_submit.occupied) {
        portEXIT_CRITICAL_SAFE(&s_btwt_submit_lock);
        return ESP_ERR_INVALID_STATE;
    }
    s_btwt_submit.occupied = true;
    s_btwt_submit.result = (esp32_mquickjs_wifi_btwt_dispatch_t){.config = options.config};
    portEXIT_CRITICAL_SAFE(&s_btwt_submit_lock);
    /* Pinned public operation 117 preserves SDK initialization, allocation,
     * AP capability checks and the synchronous ioctl wait. */
    esp_err_t error = esp_wifi_sta_btwt_setup(&s_btwt_submit.result.config);
    portENTER_CRITICAL_SAFE(&s_btwt_submit_lock);
    s_btwt_submit.result.sdk_error = error;
    bool certain = s_btwt_submit.result.native_entered ? s_btwt_submit.result.native_completed : error != ESP_OK;
    if (!certain || (error == ESP_OK && s_btwt_submit.result.identity == 0)) {
        if (s_btwt_submit.result.handoff_error == ESP_OK) s_btwt_submit.result.handoff_error = ESP_ERR_INVALID_STATE;
    }
    if (certain) *output = s_btwt_submit.result;
    else {
        /* Do not race native config writeback on an unexpected early return. */
        *output = (esp32_mquickjs_wifi_btwt_dispatch_t){.config = options.config,
            .identity = s_btwt_submit.result.identity, .sdk_error = error,
            .driver_error = s_btwt_submit.result.driver_error,
            .handoff_error = s_btwt_submit.result.handoff_error,
            .native_entered = s_btwt_submit.result.native_entered,
            .driver_called = s_btwt_submit.result.driver_called,
            .native_completed = s_btwt_submit.result.native_completed};
    }
    esp_err_t handoff_error = s_btwt_submit.result.handoff_error;
    if (certain) memset(&s_btwt_submit, 0, sizeof(s_btwt_submit));
    portEXIT_CRITICAL_SAFE(&s_btwt_submit_lock);
    return handoff_error != ESP_OK ? handoff_error : error;
}
#endif
