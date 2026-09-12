#include "esp32_mquickjs_wifi_eap_sdk.h"
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI && CONFIG_ESP_WIFI_ENTERPRISE_SUPPORT
#include "esp_eap_client.h"
struct os_reltime;
#include "utils/eloop.h"
/* Compiled inside the hash-gated SDK EAP client translation unit. */
unsigned esp32qjs_eap_native_resources(void);
int esp32qjs_eap_native_cleanup_error(void);
int esp32qjs_eap_native_control_error(void);
bool current_task_is_wifi_task(void);
_Static_assert(sizeof(esp32_mquickjs_wifi_eap_sdk_snapshot_t) == 16,
    "EAP observation flags must retain the existing snapshot budget");

static int eap_sdk_snapshot_dispatch(void *opaque, void *unused)
{
    (void)unused;
    esp32_mquickjs_wifi_eap_sdk_snapshot_t *output = opaque;
    output->resources = esp32qjs_eap_native_resources();
    output->cleanup_error = esp32qjs_eap_native_cleanup_error();
    output->control_error = esp32qjs_eap_native_control_error();
    output->time_check_known = esp_eap_client_get_disable_time_check(
        &output->disable_time_check) == ESP_OK;
    output->entered = true;
    return 0;
}

esp_err_t esp32_mquickjs_wifi_eap_sdk_snapshot(esp32_mquickjs_wifi_eap_sdk_snapshot_t *output)
{
    if (!output) return ESP_ERR_INVALID_ARG;
    *output = (esp32_mquickjs_wifi_eap_sdk_snapshot_t){
        .resources = UINT32_MAX, .cleanup_error = ESP_ERR_INVALID_STATE, .control_error = ESP_ERR_INVALID_STATE};
    /* Fixed eloop retains this stack until handler completion, or discards the
     * queued call before returning. An unknown snapshot is never idle proof. */
    int result = current_task_is_wifi_task() ? eap_sdk_snapshot_dispatch(output, NULL) :
        eloop_register_timeout_blocking(eap_sdk_snapshot_dispatch, output, NULL);
    return result == 0 && output->entered ? ESP_OK : ESP_FAIL;
}
#endif
