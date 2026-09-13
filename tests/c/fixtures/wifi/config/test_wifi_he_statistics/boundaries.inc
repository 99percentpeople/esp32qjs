
typedef unsigned esp_wifi_aci_t;
static esp32_mquickjs_wifi_he_statistics_t native_statistics;
static unsigned rx_writes,tx_writes[4],retire_calls;
static bool bad_statistics_read;
static int esp_wifi_enable_rx_statistics(bool ordinary,bool mu){
    ++rx_writes;int error=sdk_step(true);
    native_statistics.ordinary=!error && ordinary;native_statistics.multi_user=!error && mu;return error;
}
static int esp_wifi_enable_tx_statistics(esp_wifi_aci_t aci,bool enabled){
    assert(aci<4);++tx_writes[aci];int error=sdk_step(true);
    if(!error){if(enabled)native_statistics.tx_mask|=1U<<aci;else native_statistics.tx_mask&=~(1U<<aci);}return error;
}
static int esp32_mquickjs_wifi_he_statistics_snapshot(esp32_mquickjs_wifi_he_statistics_t *actual,bool retire){
    int error=sdk_step(false);if(error)return error;
    if(bad_statistics_read)return ESP_ERR_INVALID_RESPONSE;
    *actual=native_statistics;
    if(retire){++retire_calls;native_statistics=(esp32_mquickjs_wifi_he_statistics_t){0};}return ESP_OK;
}
