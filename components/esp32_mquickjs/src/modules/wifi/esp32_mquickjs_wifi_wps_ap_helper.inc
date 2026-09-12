/* Runtime-only reservation. Unlike Station WPS this neither owns a connection
 * generation nor changes DHCP/link state; APSTA's normal STA events continue. */
bool esp32_mquickjs_wifi_wps_ap_helper_held(void)
{
    wifi_lock();
    bool held = s_wifi_wps_ap_helper_identity != 0;
    wifi_unlock();
    return held;
}

esp_err_t esp32_mquickjs_wifi_wps_ap_helper_reserve(uint32_t *identity,
    esp32_mquickjs_wifi_radio_lease_t owners[3])
{
    if (!identity || *identity || !owners) return ESP_ERR_INVALID_ARG;
    if (!wifi_driver_helpers_ready()) return ESP_ERR_INVALID_STATE;
    const esp32_mquickjs_wifi_radio_lease_t *ap = esp32_mquickjs_wifi_ap_control_lease();
    if (!ap || !ap->acquired) return ESP_ERR_INVALID_STATE;
    wifi_lock();
    int error = ESP_OK;
    if (esp32_mquickjs_wifi_connection_reserved_locked() || s_wifi_state.connect_draining ||
        s_wifi_state.connect_in_progress || s_wifi_state.connect_start_active || s_wifi_state.disconnect_active ||
        s_wifi_state.scan_in_progress || s_wifi_state.scan_future_registered || s_wifi_state.scan_draining ||
        s_wifi_state.scan_results_pending || s_wifi_state.scan_start_active || s_wifi_state.scan_stop_active)
        error = ESP_ERR_INVALID_STATE;
    else if (!s_wifi_wps_ap_next_helper_identity) error = ESP_ERR_NO_MEM;
    else {
        *identity = s_wifi_wps_ap_helper_identity = s_wifi_wps_ap_next_helper_identity++;
        owners[0] = s_wifi_application;
        owners[1] = s_wifi_state.radio_lease;
        owners[2] = *ap;
    }
    wifi_unlock();
    return error;
}

esp_err_t esp32_mquickjs_wifi_wps_ap_helper_release(uint32_t *identity)
{
    if (!identity || !*identity) return ESP_ERR_INVALID_ARG;
    wifi_lock();
    bool exact = *identity == s_wifi_wps_ap_helper_identity;
    if (exact) { s_wifi_wps_ap_helper_identity = 0; *identity = 0; }
    wifi_unlock();
    return exact ? ESP_OK : ESP_ERR_INVALID_STATE;
}
