
int main(void){
    esp32_mquickjs_wifi_he_statistics_t actual;
    assert(__wrap_hal_enable_rx_statistics(true,true)==ESP_OK);
    for(unsigned i=0;i<4;++i)assert(__wrap_hal_enable_tx_statistics(i,true)==ESP_OK);
    assert(live==16);discard=true;
    assert(esp32_mquickjs_wifi_he_statistics_snapshot(&actual,true)==ESP_FAIL && live==16);
    discard=false;
    assert(esp32_mquickjs_wifi_he_statistics_snapshot(&actual,false)==ESP_OK && live==16);
    assert(actual.ordinary && actual.multi_user && actual.tx_mask==15);
    assert(esp32_mquickjs_wifi_he_statistics_snapshot(&actual,true)==ESP_OK && !live);
    assert(actual.ordinary && actual.multi_user && actual.tx_mask==15);
    in_wifi_task=true;
    assert(esp32_mquickjs_wifi_he_statistics_snapshot(&actual,true)==ESP_OK && !live);
    assert(!actual.ordinary && !actual.multi_user && !actual.tx_mask);
    assert(__wrap_hal_enable_rx_statistics(true,false)==ESP_OK);
    void *held=rx_storage[2];rx_storage[2]=NULL;
    assert(esp32_mquickjs_wifi_he_statistics_snapshot(&actual,true)==ESP_ERR_INVALID_RESPONSE && live==3);
    rx_storage[2]=held;assert(esp32_mquickjs_wifi_he_statistics_snapshot(&actual,true)==ESP_OK && !live);
    return 0;
}
