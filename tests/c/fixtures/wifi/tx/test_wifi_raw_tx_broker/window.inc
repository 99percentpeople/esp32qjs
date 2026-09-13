int main(void) {
    (void)start_callback;(void)finish_callback;(void)sending_token;
    uint8_t bytes[24]={0x80};memset(bytes+4,0x34,6);memset(bytes+10,0x12,6);
    memcpy(destination,bytes+4,6);memcpy(source_address,bytes+10,6);
    callback_info=(esp_80211_tx_info_t){.des_addr=destination,.src_addr=source_address,
        .ifidx=WIFI_IF_STA,.tx_status=WIFI_SEND_SUCCESS};
    policy_t policy={.driver_sequence=true};
    token_t tokens[8]={{0}};void *identities[8];
    esp32_mquickjs_wifi_raw_tx_validation_t validation;status_t st;
    assert(!esp32_mquickjs_wifi_raw_tx_broker_register(7));
    for(unsigned i=0;i<8;++i) {
        assert(!esp32_mquickjs_wifi_raw_tx_broker_submit(7,1,bytes,24,&policy,&tokens[i],&validation));
        identities[i]=last_descriptor;
    }
    esp32_mquickjs_wifi_raw_tx_broker_status(&st);assert(st.in_flight==8 && st.max_in_flight==8);
    token_t extra={0};assert(esp32_mquickjs_wifi_raw_tx_broker_submit(7,1,bytes,24,&policy,&extra,&validation));
    assert(!extra.identity);
    /* Identical RF bytes and addresses, deliberately reversed callbacks. */
    esp_80211_tx_info_t info;
    for(unsigned i=8;i--;) {
        esp32qjs_raw_tx_info(identities[i],&info);
        assert(esp32_mquickjs_wifi_raw_tx_broker_result(&tokens[i],&st) && st.driver_completed);
        if(i) { assert(esp32_mquickjs_wifi_raw_tx_broker_result(&tokens[i-1],&st) && !st.driver_completed); }
    }
    token_t stale=tokens[0];assert(esp32_mquickjs_wifi_raw_tx_broker_retire(&tokens[0]));
    /* Descriptor reuse while older completed records still await retirement. */
    next_descriptor=0;synchronous=true;
    assert(!esp32_mquickjs_wifi_raw_tx_broker_submit(7,1,bytes,24,&policy,&tokens[0],&validation));
    assert(!esp32_mquickjs_wifi_raw_tx_broker_retire(&stale));
    assert(esp32_mquickjs_wifi_raw_tx_broker_result(&tokens[0],&st) && st.driver_completed && st.driver_accepted);
    for(unsigned i=0;i<8;++i)assert(esp32_mquickjs_wifi_raw_tx_broker_retire(&tokens[i]));
    assert(allocations==frees);
    synchronous=false;
    for(unsigned i=0;i<8;++i)assert(!esp32_mquickjs_wifi_raw_tx_broker_submit(7,1,bytes,24,&policy,&tokens[i],&validation));
    assert(esp32_mquickjs_wifi_raw_tx_broker_abandon(&tokens[3]));
    assert(!esp32_mquickjs_wifi_raw_tx_broker_retire(&tokens[3]));
    assert(!esp32_mquickjs_wifi_raw_tx_broker_quiesce(7,&tokens[3]));
    assert(esp32_mquickjs_wifi_raw_tx_broker_reset_after_deinit(7));
    assert(allocations==frees);
    for(unsigned i=0;i<8;++i) {
        assert(esp32_mquickjs_wifi_raw_tx_broker_result(&tokens[i],&st) && st.native_terminated);
        assert(esp32_mquickjs_wifi_raw_tx_broker_retire(&tokens[i]));
    }
    return 0;
}
