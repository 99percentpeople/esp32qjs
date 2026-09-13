
static bool s_wifi_station_capture_dpp;
static esp_err_t esp32_mquickjs_wifi_radio_dpp_connection_begin(const esp32_mquickjs_wifi_radio_operation_t *owner){
 assert(s_wifi_wps_capture_identity==7&&s_wifi_station_capture_dpp&&owner->kind==ESP32_MQUICKJS_WIFI_RADIO_OPERATION_DPP);
 return esp32_mquickjs_wifi_radio_smartconfig_connection_begin(owner);
}
static esp_err_t esp32_mquickjs_wifi_radio_dpp_connection_end(const esp32_mquickjs_wifi_radio_operation_t *owner){
 assert(s_wifi_wps_capture_identity==7&&s_wifi_station_capture_dpp&&owner->kind==ESP32_MQUICKJS_WIFI_RADIO_OPERATION_DPP);
 return esp32_mquickjs_wifi_radio_smartconfig_connection_end(owner);
}
