#include "esp32_mquickjs_wifi_rrm_sdk.h"
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI && CONFIG_ESP_WIFI_RRM_SUPPORT
struct os_reltime;
#include "utils/eloop.h"
/* These helpers are compiled in the exact SDK rrm.c translation unit by the
 * hash-gated build-local patch. No supplicant structure ABI is duplicated here. */
int esp32qjs_rrm_request(esp32_mquickjs_wifi_rrm_callback_t cb, void *identity, bool *tx_attempted);
int esp32qjs_rrm_cancel(esp32_mquickjs_wifi_rrm_callback_t cb, void *identity);
int esp32qjs_rrm_query(esp32_mquickjs_wifi_rrm_callback_t cb, void *identity);
typedef struct {
    esp32_mquickjs_wifi_rrm_command_t command;
    esp32_mquickjs_wifi_rrm_callback_t callback;
    void *identity;
    esp32_mquickjs_wifi_rrm_sdk_result_t *result;
} rrm_sdk_call_t;

static int rrm_sdk_dispatch(void *opaque, void *unused)
{
    (void)unused;
    rrm_sdk_call_t *call = opaque;
    esp32_mquickjs_wifi_rrm_sdk_result_t *r = call->result;
    r->entered = true;
    switch (call->command) {
    case ESP32_MQUICKJS_WIFI_RRM_SUBMIT:
        r->code = esp32qjs_rrm_request(call->callback, call->identity, &r->tx_attempted);
        break;
    case ESP32_MQUICKJS_WIFI_RRM_CANCEL:
        r->code = esp32qjs_rrm_cancel(call->callback, call->identity);
        break;
    case ESP32_MQUICKJS_WIFI_RRM_QUERY:
        r->code = esp32qjs_rrm_query(call->callback, call->identity);
        break;
    }
    r->ownership = esp32qjs_rrm_query(call->callback, call->identity);
    return 0;
}

esp_err_t esp32_mquickjs_wifi_rrm_sdk_command(esp32_mquickjs_wifi_rrm_command_t command,
    esp32_mquickjs_wifi_rrm_callback_t callback, void *identity,
    esp32_mquickjs_wifi_rrm_sdk_result_t *result)
{
    if (result == NULL) return ESP_ERR_INVALID_ARG;
    *result = (esp32_mquickjs_wifi_rrm_sdk_result_t){.ownership = -1};
    if ((unsigned)command > ESP32_MQUICKJS_WIFI_RRM_QUERY || callback == NULL || identity == NULL)
        return ESP_ERR_INVALID_ARG;
    rrm_sdk_call_t call = {command, callback, identity, result};
    /* The fixed SDK waits until dispatch completes or is destroyed without
     * execution. Its waiter keeps this stack valid throughout either path. */
    int rc = eloop_register_timeout_blocking(rrm_sdk_dispatch, &call, NULL);
    return rc == 0 && result->entered ? ESP_OK : ESP_FAIL;
}
#endif
