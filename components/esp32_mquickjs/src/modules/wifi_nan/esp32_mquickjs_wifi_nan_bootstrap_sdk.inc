/* Submit-only counterpart of the SDK's shared-event-group bootstrap sender.
 * The framework retains its message/context until real TX callback and recycle. */
_Static_assert((int)WIFI_NAN_NPBA_TYPE_REQUEST == ESP32_MQUICKJS_NAN_BOOTSTRAP_REQUEST &&
    (int)WIFI_NAN_NPBA_TYPE_RESPONSE == ESP32_MQUICKJS_NAN_BOOTSTRAP_RESPONSE &&
    (int)WIFI_NAN_PAIRING_STATUS_ACCEPTED == ESP32_MQUICKJS_NAN_BOOTSTRAP_ACCEPTED &&
    (int)WIFI_NAN_PAIRING_STATUS_REJECTED == ESP32_MQUICKJS_NAN_BOOTSTRAP_REJECTED &&
    (int)WIFI_NAN_PAIRING_STATUS_COMEBACK == ESP32_MQUICKJS_NAN_BOOTSTRAP_COMEBACK, "review NPBA values");

esp_err_t esp32_mquickjs_wifi_nan_sdk_bootstrap_send(wifi_nan_followup_params_t *params,
    uint32_t *context, bool response, bool accept)
{
    if (!params || !context || *context || !params->inst_id || !params->peer_inst_id ||
        params->ssi_len || params->ssi || params->vendor_ie || (params->peer_mac[0] & 1) ||
        !memcmp(params->peer_mac, (uint8_t[6]){0}, 6)) return ESP_ERR_INVALID_ARG;
    if (!s_nan_data_lock) return ESP_ERR_INVALID_STATE;
    NAN_DATA_LOCK();
    const struct own_svc_info *own = nan_find_own_svc(params->inst_id);
    bool valid = own && own->pairing.pairing_setup &&
        own->type == (response ? ESP_NAN_PUBLISH : ESP_NAN_SUBSCRIBE) &&
        nan_find_peer_svc_exact(params->inst_id, params->peer_inst_id, params->peer_mac);
    NAN_DATA_UNLOCK();
    if (!valid) return ESP_ERR_INVALID_STATE;
    wifi_nan_pairing_npba_params_t npba = {
        .type = response ? WIFI_NAN_NPBA_TYPE_RESPONSE : WIFI_NAN_NPBA_TYPE_REQUEST,
        .status = !response || accept ? WIFI_NAN_PAIRING_STATUS_ACCEPTED : WIFI_NAN_PAIRING_STATUS_REJECTED,
        .method = response ? WIFI_NAN_BOOTSTRAP_PIN_CODE_DISPLAY : WIFI_NAN_BOOTSTRAP_PIN_CODE_KEYPAD,
    };
    extra_params_internal_t extra = {.pairing_npba = &npba};
    return esp_nan_internal_send_followup(params, context, &extra);
}
