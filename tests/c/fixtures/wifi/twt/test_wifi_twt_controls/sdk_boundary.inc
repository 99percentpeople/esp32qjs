
static wifi_twt_config_t native_twt_policy;
static int esp32_mquickjs_wifi_twt_sdk_control(esp32_mquickjs_wifi_twt_control_kind_t kind,
    esp32_mquickjs_wifi_twt_control_t *value){
    bool write=kind==ESP32_MQUICKJS_WIFI_TWT_WRITE_CONFIG || kind==ESP32_MQUICKJS_WIFI_TWT_WRITE_OFFSET;
    int error=sdk_step(write);if(error)return error;
    if(kind==ESP32_MQUICKJS_WIFI_TWT_WRITE_CONFIG)native_twt_policy=value->config;
    if(kind<=ESP32_MQUICKJS_WIFI_TWT_WRITE_CONFIG)value->config=native_twt_policy;
    if(kind==ESP32_MQUICKJS_WIFI_TWT_READ_FLOWS)value->flow_bitmap=129;
    return ESP_OK;
}
