#include "esp32_mquickjs_wifi_wapi.h"
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI && CONFIG_ESP_WIFI_WAPI_PSK
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include <limits.h>

static portMUX_TYPE s_wapi_lock = portMUX_INITIALIZER_UNLOCKED;
static esp32_mquickjs_wifi_wapi_status_t s_wapi = {.requested_enabled = true, .revision = 1};
/* The reviewed SDK can free native state before callback unregistration
 * returns an error. Never replay that uncertain deinit suffix. */
static bool s_wapi_deinit_attempted;
esp_err_t __real_esp_wifi_internal_wapi_init(void);
esp_err_t __real_esp_wifi_internal_wapi_deinit(void);

void esp32_mquickjs_wifi_wapi_sdk_status(esp32_mquickjs_wifi_wapi_status_t *out)
{
    portENTER_CRITICAL(&s_wapi_lock); *out = s_wapi; portEXIT_CRITICAL(&s_wapi_lock);
}

esp_err_t esp32_mquickjs_wifi_wapi_sdk_policy(bool enabled)
{
    portENTER_CRITICAL(&s_wapi_lock);
    esp_err_t error = s_wapi.busy || s_wapi.uncertain ? ESP_ERR_INVALID_STATE : ESP_OK;
    if (!error && s_wapi.requested_enabled != enabled) {
        if (s_wapi.revision == UINT32_MAX) error = ESP_ERR_NO_MEM;
        else { ++s_wapi.revision; s_wapi.requested_enabled = enabled; }
    }
    portEXIT_CRITICAL(&s_wapi_lock);
    return error;
}

esp_err_t __wrap_esp_wifi_internal_wapi_init(void)
{
    portENTER_CRITICAL(&s_wapi_lock);
    esp_err_t error = s_wapi.busy || s_wapi.supplicant_active || s_wapi.uncertain ?
        ESP_ERR_INVALID_STATE : s_wapi.generation == UINT32_MAX ? ESP_ERR_NO_MEM : ESP_OK;
    bool requested = s_wapi.requested_enabled;
    if (!error) { s_wapi.busy = true; ++s_wapi.generation; s_wapi_deinit_attempted = false; }
    portEXIT_CRITICAL(&s_wapi_lock);
    if (error) return error;
    error = requested ? __real_esp_wifi_internal_wapi_init() : ESP_OK;
    portENTER_CRITICAL(&s_wapi_lock);
    s_wapi.busy = false; s_wapi.supplicant_active = !error;
    s_wapi.enabled = requested && !error; s_wapi.error = error; s_wapi.cleanup_error = ESP_OK;
    /* Raw -1/-2/-3 are the hash-reviewed allocator failures; each frees its
     * prefix before publishing native state. Do not guess about other errors. */
    if (error && error != -1 && error != -2 && error != -3) s_wapi.uncertain = true;
    portEXIT_CRITICAL(&s_wapi_lock);
    return error;
}

esp_err_t __wrap_esp_wifi_internal_wapi_deinit(void)
{
    portENTER_CRITICAL(&s_wapi_lock);
    esp_err_t error = s_wapi.busy || s_wapi.uncertain ? ESP_ERR_INVALID_STATE : ESP_OK;
    bool release = s_wapi.enabled;
    if (!error && release && s_wapi_deinit_attempted) error = ESP_ERR_INVALID_STATE;
    if (!error) { s_wapi.busy = true; if (release) s_wapi_deinit_attempted = true; }
    portEXIT_CRITICAL(&s_wapi_lock);
    if (error) return error;
    error = release ? __real_esp_wifi_internal_wapi_deinit() : ESP_OK;
    portENTER_CRITICAL(&s_wapi_lock);
    s_wapi.busy = false; s_wapi.cleanup_error = error;
    if (error) s_wapi.uncertain = true;
    else { s_wapi.enabled = false; s_wapi.supplicant_active = false; }
    portEXIT_CRITICAL(&s_wapi_lock);
    return error;
}

esp_err_t esp32_mquickjs_wifi_wapi_sdk_cleanup_error(void)
{
    portENTER_CRITICAL(&s_wapi_lock);
    esp_err_t error = s_wapi.cleanup_error ? s_wapi.cleanup_error :
        s_wapi.uncertain || s_wapi.busy || s_wapi.supplicant_active ? ESP_ERR_INVALID_STATE : ESP_OK;
    portEXIT_CRITICAL(&s_wapi_lock);
    return error;
}
#endif
