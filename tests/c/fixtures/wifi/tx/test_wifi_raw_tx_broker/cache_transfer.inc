int main(void) {
    (void)start_callback; (void)finish_callback; (void)sending_token;
    uint8_t bytes[24]={0x40};
    memset(bytes+4,0x34,6);memset(bytes+10,0x12,6);
    memcpy(destination,bytes+4,6);memcpy(source_address,bytes+10,6);
    callback_info=(esp_80211_tx_info_t){.des_addr=destination,.src_addr=source_address,
        .ifidx=WIFI_IF_STA,.tx_status=WIFI_SEND_SUCCESS};
    policy_t policy={.driver_sequence=true};
    token_t tokens[8]={{0}};void *completed_descriptors[8];
    esp32_mquickjs_wifi_raw_tx_validation_t validation;status_t st;
    assert(esp32_mquickjs_wifi_raw_tx_broker_register(7)==ESP_OK);
    for(unsigned i=0;i<4;i++) {
        assert(esp32_mquickjs_wifi_raw_tx_broker_submit(7,1,bytes,24,&policy,&tokens[i],&validation)==ESP_OK);
        completed_descriptors[i]=&descriptors[8+i];
        esp32qjs_raw_tx_copy_header(completed_descriptors[i],last_descriptor);
        assert(esp32_mquickjs_wifi_raw_tx_broker_result(&tokens[i],&st) && !st.driver_completed);
        assert(!st.registration_uncertain && !st.correlation_failure_reason);
    }
    /* Original cache descriptors may be reused before their copied TX completes. */
    next_descriptor=0;
    for(unsigned i=4;i<8;i++) {
        assert(esp32_mquickjs_wifi_raw_tx_broker_submit(7,1,bytes,24,&policy,&tokens[i],&validation)==ESP_OK);
        completed_descriptors[i]=last_descriptor;
    }
    /* Untracked SDK traffic must not adopt a Raw TX identity or poison it. */
    esp32qjs_raw_tx_copy_header(&descriptors[14],&descriptors[15]);
    esp_80211_tx_info_t info;
    for(unsigned i=8;i--;) {
        esp32qjs_raw_tx_info(completed_descriptors[i],&info);
        assert(esp32_mquickjs_wifi_raw_tx_broker_result(&tokens[i],&st));
        assert(st.driver_completed && !st.correlation_fault && !st.registration_uncertain);
        assert(!st.orphan_callbacks && !st.mismatched_callbacks && !st.correlation_failure_reason);
        assert(esp32_mquickjs_wifi_raw_tx_broker_retire(&tokens[i]));
    }
    assert(allocations==frees);
    /* A destination still bound to another operation is not safe to adopt. */
    for(unsigned i=0;i<2;i++) {
        assert(esp32_mquickjs_wifi_raw_tx_broker_submit(7,1,bytes,24,&policy,&tokens[i],&validation)==ESP_OK);
        completed_descriptors[i]=last_descriptor;
    }
    esp32qjs_raw_tx_copy_header(completed_descriptors[1],completed_descriptors[0]);
    for(unsigned i=0;i<2;i++) {
        assert(esp32_mquickjs_wifi_raw_tx_broker_result(&tokens[i],&st));
        assert(st.correlation_fault && st.quarantined && st.registration_uncertain);
        assert(!strcmp(st.correlation_failure_reason,"descriptor-transfer"));
        assert(st.correlation_failure_identity==tokens[0].identity);
        assert(!esp32_mquickjs_wifi_raw_tx_broker_retire(&tokens[i]));
    }
    assert(esp32_mquickjs_wifi_raw_tx_broker_reset_after_deinit(7));
    for(unsigned i=0;i<2;i++) assert(esp32_mquickjs_wifi_raw_tx_broker_retire(&tokens[i]));
    assert(allocations==frees);
    return 0;
}
