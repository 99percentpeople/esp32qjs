/* Included after SDK AP command bodies. No IPC, public roots or cross-task
 * pointer access here; the retained worker supplies ordered dispatch. */
#include "esp32_mquickjs_wifi_wps_ap_sdk.h"
#include "esp_dpp_i.h"

static bool esp32qjs_wps_ap_managed(uint32_t identity)
{
    return esp32qjs_wps_ap_result_exact(identity) && s_wps_ap_result->status.retained;
}

static void esp32qjs_wps_ap_command_error(esp_err_t error)
{
    if (error == ESP_OK) return;
    esp32_mquickjs_wifi_wps_ap_result_status_t *status = &s_wps_ap_result->status;
    if (!status->terminal) {
        status->error = error;
        status->event_id = WIFI_EVENT_AP_WPS_RG_FAILED;
        status->terminal = true;
        esp32qjs_wps_ap_result_scrub();
    }
}

esp_err_t esp32qjs_wps_ap_native_begin(const esp_wps_config_t *config, uint32_t *identity)
{
    if (!current_task_is_wifi_task()) return ESP_ERR_INVALID_STATE;
    if (!config || !identity || *identity ||
        (config->wps_type != WPS_TYPE_PBC && config->wps_type != WPS_TYPE_PIN))
        return ESP_ERR_INVALID_ARG;
    if (wps_get_type() != WPS_TYPE_DISABLE || wps_get_status() != WPS_STATUS_DISABLE ||
        is_dpp_enabled()) return ESP_ERR_INVALID_STATE;
    /* Worker validates factory strings and the PIN before reserving Radio.
     * SDK admission below validates current AP mode/authentication as well. */
    int error = esp32qjs_wps_ap_result_reserve(identity);
    if (error != ESP_OK) return error;
    error = wifi_ap_wps_enable_internal(config, *identity);
    if (error == ESP_OK && s_wps_ap_result->status.error != ESP_OK)
        error = s_wps_ap_result->status.error;
    esp32qjs_wps_ap_command_error(error);
    s_wps_ap_result->status.enabled = error == ESP_OK;
    /* Preserve even a pre-bind failure for the exact caller's cleanup. */
    return error;
}

esp_err_t esp32qjs_wps_ap_native_start(uint32_t identity)
{
    if (!esp32qjs_wps_ap_managed(identity) || !s_wps_ap_result->status.enabled ||
        s_wps_ap_result->status.closing || s_wps_ap_result->status.terminal ||
        s_wps_ap_result->start_attempted) return ESP_ERR_INVALID_STATE;
    s_wps_ap_result->start_attempted = true;
    /* PIN already belongs to the SDK protocol object; no borrowed string. */
    int error = wifi_ap_wps_start_internal(NULL, identity);
    if (error == ESP_OK && s_wps_ap_result->status.error != ESP_OK)
        error = s_wps_ap_result->status.error;
    esp32qjs_wps_ap_command_error(error);
    s_wps_ap_result->status.started = error == ESP_OK;
    return error;
}

esp_err_t esp32qjs_wps_ap_native_stop(uint32_t identity)
{
    if (!esp32qjs_wps_ap_managed(identity)) return ESP_ERR_INVALID_STATE;
    esp32qjs_wps_ap_result_close_intent(identity);
    if (s_wps_ap_result->status.sdk_attached)
        return esp32qjs_wps_ap_result_prepare_close(s_wps_ap_result->context);
    /* A failed enable before bind has no SDK mutations to undo. */
    if (!s_wps_ap_result->ever_bound || s_wps_ap_result->status.sdk_state_retired) {
        s_wps_ap_result->status.close_prepared = true;
        return ESP_OK;
    }
    return ESP_ERR_INVALID_STATE;
}

esp_err_t esp32qjs_wps_ap_native_retire(uint32_t identity)
{
    if (!esp32qjs_wps_ap_managed(identity)) return ESP_ERR_INVALID_STATE;
    esp32_mquickjs_wifi_wps_ap_result_status_t *status = &s_wps_ap_result->status;
    if (!status->closing || !status->close_prepared || status->callback_depth ||
        status->tracking_fault || s_wps_ap_result->retiring) return ESP_ERR_INVALID_STATE;
    if (status->sdk_state_retired) return ESP_OK;
    int error = ESP_OK;
    if (status->sdk_attached) {
        if (s_wps_ap_result->context != hostapd_get_hapd_data() ||
            (wps_get_owner() != WPS_OWNER_REGISTRAR && wps_get_owner() != WPS_OWNER_NONE))
            return ESP_ERR_INVALID_STATE;
        s_wps_ap_result->retiring = true;
        error = wifi_ap_wps_deinit();
        s_wps_ap_result->retiring = false;
        if (error != ESP_OK) goto done;
        if (status->sdk_attached || gWpsSm) { error = ESP_ERR_INVALID_STATE; goto done; }
        wps_set_owner(WPS_OWNER_NONE);
        s_wps_ap_result->heap_retired = true;
    } else if (s_wps_ap_result->ever_bound && !s_wps_ap_result->heap_retired) {
        /* Do not accept an unexpected external detach as a retire receipt. */
        return ESP_ERR_INVALID_STATE;
    }
    error = esp32qjs_wps_ap_factory_release(identity);
    if (error != ESP_OK) goto done;
    status->sdk_state_retired = true;
done:
    status->cleanup_error = error;
    return error;
}

esp_err_t esp32qjs_wps_ap_native_checkpoint(uint32_t identity, uint32_t *revision)
{
    if (!revision) return ESP_ERR_INVALID_ARG;
    if (!esp32qjs_wps_ap_managed(identity)) return ESP_ERR_INVALID_STATE;
    const esp32_mquickjs_wifi_wps_ap_result_status_t *status = &s_wps_ap_result->status;
    if (!status->closing || !status->close_prepared || !status->sdk_state_retired ||
        status->sdk_attached || status->callback_depth || status->tracking_fault ||
        s_wps_ap_result->retiring || gWpsSm || esp32qjs_wps_native_held() ||
        wps_get_owner() != WPS_OWNER_NONE || wps_get_type() != WPS_TYPE_DISABLE ||
        wps_get_status() != WPS_STATUS_DISABLE || !esp32qjs_wps_ap_eapol_drained(identity))
        return ESP_ERR_INVALID_STATE;
    *revision = s_wps_ap_result->callback_revision;
    return ESP_OK;
}

esp_err_t esp32qjs_wps_ap_native_release(uint32_t identity, uint32_t revision)
{
    uint32_t current;
    int error = esp32qjs_wps_ap_native_checkpoint(identity, &current);
    if (error != ESP_OK) return error;
    if (current != revision) return ESP_ERR_NOT_FINISHED;
    if (s_wps_ap_result->status.pin_available) return ESP_ERR_INVALID_STATE;
    esp32qjs_wps_ap_result_free();
    return ESP_OK;
}
