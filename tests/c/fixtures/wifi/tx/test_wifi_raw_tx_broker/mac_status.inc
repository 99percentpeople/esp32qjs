/* Exercise the production callback hook and both target descriptor layouts. */
int main(void) {
    (void)start_callback; (void)finish_callback; (void)sending_token;
    uint8_t bytes[24]={0x40};
    memset(bytes+4,0x34,6);memset(bytes+10,0x12,6);
    memcpy(destination,bytes+4,6);memcpy(source_address,bytes+10,6);
    callback_info=(esp_80211_tx_info_t){.des_addr=destination,.src_addr=source_address,.ifidx=WIFI_IF_STA};
    policy_t policy={.driver_sequence=true};
    assert(esp32_mquickjs_wifi_raw_tx_broker_register(7)==ESP_OK);
    assert(raw_tx_read_mac_status_byte(NULL)==-1);
    descriptor_fixture_t empty={0};
    assert(raw_tx_read_mac_status_byte(&empty)==-1);
    for(unsigned code=0;code<256;code++) {
        token_t token={0};status_t st;esp32_mquickjs_wifi_raw_tx_validation_t validation;
        callback_info.tx_status=code==1 ? WIFI_SEND_SUCCESS : WIFI_SEND_FAIL;
        assert(esp32_mquickjs_wifi_raw_tx_broker_submit(7,1,bytes,24,&policy,&token,&validation)==ESP_OK);
        descriptor_fixture_t *descriptor=last_descriptor;
        descriptor->tx_info[19]=(uint8_t)code;
        esp_80211_tx_info_t info;
        esp32qjs_raw_tx_info(descriptor,&info);
        memset(descriptor->tx_info,0xa5,sizeof(descriptor->tx_info));
        assert(esp32_mquickjs_wifi_raw_tx_broker_result(&token,&st));
        assert(st.driver_completed && !st.correlation_fault && st.completion.mac_status_available);
        assert(st.completion.mac_status_byte==code && st.completion.raw_status==(int32_t)callback_info.tx_status);
        assert(st.completion.status==(code==1 ? ESP32_MQUICKJS_WIFI_RAW_TX_DRIVER_SUCCESS : ESP32_MQUICKJS_WIFI_RAW_TX_DRIVER_FAILED));
        assert(esp32_mquickjs_wifi_raw_tx_broker_retire(&token));
    }
    assert(allocations==frees);
    return 0;
}
