
#define ESP_ERR_NOT_FINISHED 0x104
void esp32_mquickjs_wifi_twt_tx_cleanup_native(void);
esp_err_t esp32_mquickjs_wifi_twt_tx_information_quiescent_native(uint32_t);
void esp32_mquickjs_wifi_twt_information_timer_snapshot(esp32_mquickjs_wifi_twt_information_timer_snapshot_t *out) {
    assert(!locked);*out=(esp32_mquickjs_wifi_twt_information_timer_snapshot_t){0};
}
