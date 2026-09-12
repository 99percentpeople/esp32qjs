/* Runtime-only calls; Wi-Fi callbacks observe the reservation under wifi_lock.
 * Do not hold wifi_lock over driver/netif/Radio calls. */
static bool s_wifi_station_capture_dpp;
static esp_err_t wifi_station_capture_reserve(bool dpp, uint32_t *identity,
    esp32_mquickjs_wifi_radio_lease_t owners[3])
{
    if (!identity || *identity || !owners) return ESP_ERR_INVALID_ARG;
    if (!wifi_driver_helpers_ready() || !wifi_helpers_idle(false) || !s_wifi_state.sta_netif ||
        !s_wifi_state.radio_lease.acquired) return ESP_ERR_INVALID_STATE;
    esp_err_t error = esp32_mquickjs_wifi_prepare_connect_timer();
    if (error != ESP_OK) return error;
    wifi_lock();
    if (s_wifi_wps_capture_identity || esp32_mquickjs_wifi_connection_reserved_locked() ||
        s_wifi_state.connect_draining || s_wifi_state.scan_future_registered ||
        s_wifi_state.scan_in_progress || s_wifi_state.status.connected) error = ESP_ERR_INVALID_STATE;
    else if (!s_wifi_wps_next_identity || s_wifi_state.connect_generation == UINT32_MAX)
        error = ESP_ERR_NO_MEM;
    else {
        s_wifi_wps_capture_identity = s_wifi_wps_next_identity++;
        s_wifi_station_capture_dpp = dpp;
        ++s_wifi_state.connect_generation;
        *identity = s_wifi_wps_capture_identity;
        owners[0] = s_wifi_application;
        owners[1] = s_wifi_state.radio_lease;
    }
    wifi_unlock();
    if (error == ESP_OK) {
        const esp32_mquickjs_wifi_radio_lease_t *ap = esp32_mquickjs_wifi_ap_control_lease();
        owners[2] = ap ? *ap : (esp32_mquickjs_wifi_radio_lease_t){0};
    }
    return error;
}

static esp_err_t wifi_station_capture_drain(bool dpp, uint32_t identity)
{
    wifi_lock();
    bool exact = identity && identity == s_wifi_wps_capture_identity && s_wifi_station_capture_dpp == dpp;
    bool conflict = s_wifi_state.connect_future_registered || s_wifi_smartconfig_connect.owner.identity ||
        s_wifi_state.scan_future_registered || s_wifi_state.connect_in_progress ||
        s_wifi_state.connect_start_active || s_wifi_state.scan_start_active;
    bool exhausted = s_wifi_state.disconnect_epoch_exhausted;
    wifi_unlock();
    if (!exact || conflict || exhausted) return ESP_ERR_INVALID_STATE;
    /* Never force-disconnect a later connection. The native capture owner has
     * already stopped and completed its event fence. Default
     * STA handlers and the existing link fence finish the DHCP/IP suffix. */
    esp_err_t error = wifi_post_disconnect_fence();
    if (error != ESP_OK) return error;
    if (!s_wifi_state.sta_netif || esp_netif_is_netif_up(s_wifi_state.sta_netif))
        return ESP_ERR_NOT_FINISHED;
    wifi_lock();
    bool pending = s_wifi_state.connect_draining || s_wifi_state.disconnect_active ||
        s_wifi_state.status.connected || s_wifi_state.scan_in_progress ||
        s_wifi_state.scan_draining || s_wifi_state.scan_results_pending || s_wifi_state.scan_stop_active;
    wifi_unlock();
    return pending ? ESP_ERR_NOT_FINISHED : ESP_OK;
}

static esp_err_t wifi_station_capture_release(bool dpp, uint32_t *identity)
{
    if (!identity || !*identity) return ESP_ERR_INVALID_ARG;
    esp_err_t error = wifi_station_capture_drain(dpp, *identity);
    if (error != ESP_OK) return error;
    wifi_lock();
    if (*identity != s_wifi_wps_capture_identity || s_wifi_station_capture_dpp != dpp) error = ESP_ERR_INVALID_STATE;
    else { s_wifi_wps_capture_identity = 0; s_wifi_station_capture_dpp = false; *identity = 0; }
    wifi_unlock();
    return error;
}

esp_err_t esp32_mquickjs_wifi_wps_station_reserve(uint32_t *identity, esp32_mquickjs_wifi_radio_lease_t owners[3])
{ return wifi_station_capture_reserve(false, identity, owners); }
esp_err_t esp32_mquickjs_wifi_wps_station_drain(uint32_t identity)
{ return wifi_station_capture_drain(false, identity); }
esp_err_t esp32_mquickjs_wifi_wps_station_release(uint32_t *identity)
{ return wifi_station_capture_release(false, identity); }
#if CONFIG_ESP_WIFI_DPP_SUPPORT
esp_err_t esp32_mquickjs_wifi_dpp_station_reserve(uint32_t *identity, esp32_mquickjs_wifi_radio_lease_t owners[3])
{ return wifi_station_capture_reserve(true, identity, owners); }
esp_err_t esp32_mquickjs_wifi_dpp_station_drain(uint32_t identity)
{ return wifi_station_capture_drain(true, identity); }
esp_err_t esp32_mquickjs_wifi_dpp_station_release(uint32_t *identity)
{ return wifi_station_capture_release(true, identity); }
#endif
