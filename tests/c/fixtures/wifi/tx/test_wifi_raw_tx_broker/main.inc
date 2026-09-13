
#define broker(name) esp32_mquickjs_wifi_raw_tx_broker_##name
static uint8_t bytes[24]={0x80};
static policy_t policy={.interface=ESP32_MQUICKJS_WIFI_RAW_TX_STATION,.driver_sequence=true};
static esp_err_t submit(token_t *token) {
    sending_token=token;esp32_mquickjs_wifi_raw_tx_validation_t validation;
    esp_err_t error=broker(submit)(7,123,bytes,sizeof(bytes),&policy,token,&validation);
    assert(validation==ESP32_MQUICKJS_WIFI_RAW_TX_VALID);sending_token=NULL;return error;
}
static status_t status(void) {status_t result;broker(status)(&result);return result;}
static void retire(token_t *token) {
    token_t stale=*token;unsigned before=frees;
    assert(broker(retire)(token) && !token->identity && frees==before+1);
    assert(!broker(retire)(&stale) && !broker(abandon)(&stale));
    assert(allocations==frees && !status().operation_active);
}
static void consume_termination(token_t *token) {
    unsigned freed=frees;token_t stale=*token,wrong=*token;wrong.identity++;
    assert(status().native_terminated && !status().operation_active && status().token.identity==token->identity);
    assert(broker(register)(7)==ESP_ERR_INVALID_STATE && broker(register)(8)==ESP_ERR_INVALID_STATE);
    assert(!broker(retire)(&wrong));
    assert(broker(reset_after_deinit)(token->generation) && status().native_terminated && frees==freed);
    assert(broker(retire)(token) && !token->identity && frees==freed && !status().native_terminated);
    assert(!broker(retire)(&stale) && !broker(abandon)(&stale));
}
int main(void) {
    memset(bytes+4,0x34,6);memset(bytes+10,0x12,6);
    memcpy(destination,bytes+4,6);memcpy(source_address,bytes+10,6);
    callback_info=(esp_80211_tx_info_t){.des_addr=destination,.src_addr=source_address,
        .ifidx=WIFI_IF_STA,.tx_status=WIFI_SEND_SUCCESS,.data=(uint8_t *)(uintptr_t)1,.data_len=255};
    token_t token={0},next={0};
    assert(broker(register)(0)==ESP_ERR_INVALID_ARG);
    register_error=77;
    assert(broker(register)(7)==77 && status().registration_uncertain);
    assert(broker(register)(7)==ESP_ERR_INVALID_STATE && registrations==1);
    assert(submit(&token)==ESP_ERR_INVALID_STATE && !token.identity && allocations==frees);
    unregister_error=78;
    assert(broker(unregister)(7)==78 && status().generation==7);
    unregister_error=0;
    assert(broker(unregister)(7)==ESP_OK && status().unregister_written);
    unsigned calls=unregisters;
    assert(broker(unregister)(7)==ESP_OK && unregisters==calls);
    assert(broker(register)(7)==ESP_ERR_INVALID_STATE);
    assert(broker(register)(8)==ESP_ERR_INVALID_STATE);
    assert(!broker(reset_after_deinit)(8) && broker(reset_after_deinit)(7));
    register_error=0;
    assert(broker(register)(7)==ESP_OK);
    calls=registrations;assert(broker(register)(7)==ESP_OK && registrations==calls);
    esp32_mquickjs_wifi_raw_tx_validation_t validation;
    calls=sends;unsigned allocated=allocations;
    assert(broker(submit)(7,123,bytes,23,&policy,&token,&validation)==ESP_ERR_INVALID_ARG);
    assert(sends==calls && allocations==allocated && !token.identity);
    fail_alloc=true;assert(submit(&token)==ESP_ERR_NO_MEM && !token.identity);fail_alloc=false;
    synchronous=true;
    assert(submit(&token)==ESP_OK && status().driver_completed && status().driver_accepted);
    assert(driver_bytes!=bytes && !memcmp(driver_bytes,bytes,24));
    assert(status().completion.raw_body_length==255 && status().completion.source[0]==0x12);
    token_t stale=token;retire(&token);
    synchronous=false;assert(submit(&token)==ESP_OK && token.identity!=stale.identity);
    assert(!broker(abandon)(&stale) && !broker(retire)(&stale));
    token_t wrong=token;++wrong.radio_lease_identity;assert(!broker(abandon)(&wrong));
    assert(broker(abandon)(&token) && status().quarantined);
    assert(submit(&next)==ESP_ERR_INVALID_STATE && !next.identity);
    assert(broker(unregister)(7)==ESP_ERR_INVALID_STATE && !broker(retire)(&token));
    /* Public timeout/close keeps the driver's private copy intact. */
    bytes[23]=99;assert(driver_bytes[23]==0);bytes[23]=0;
    driver_callback(NULL);assert(status().invalid_callbacks==1 && !status().driver_completed);
    destination[0]^=1;driver_callback(&callback_info);destination[0]^=1;
    callback_info.ifidx=WIFI_IF_AP;driver_callback(&callback_info);callback_info.ifidx=WIFI_IF_STA;
    assert(status().mismatched_callbacks==2 && !status().driver_completed);
    start_callback();
    assert(status().callbacks_active==1 && !broker(retire)(&token));
    assert(!broker(reset_after_deinit)(7) && broker(unregister)(7)==ESP_ERR_INVALID_STATE);
    assert(allocations==frees+1 && !memcmp(driver_bytes,bytes,24));
    finish_callback();assert(status().driver_completed && !status().quarantined);
    assert(status().abandoned);retire(&token);
    /* Submission failure cannot prove that native storage is unreferenced. */
    send_error=81;assert(submit(&token)==81 && token.identity);
    assert(status().quarantined && !status().driver_accepted && status().submit_error==81);
    assert(!broker(retire)(&token) && broker(unregister)(7)==ESP_ERR_INVALID_STATE);
    driver_callback(&callback_info);
    assert(status().correlation_fault && !broker(retire)(&token));
    token_t wrong_recovery=token;++wrong_recovery.radio_lease_identity;
    assert(broker(quiesce)(7,&wrong_recovery)==ESP_ERR_INVALID_STATE && broker(quiesce)(7,NULL)==ESP_ERR_INVALID_ARG);
    calls=unregisters;unregister_error=82;
    assert(broker(quiesce)(7,&token)==82 && status().operation_active && !status().native_terminated && allocations==frees+1);
    unregister_error=0;assert(broker(quiesce)(7,&token)==ESP_OK && unregisters==calls+2);
    assert(broker(quiesce)(7,&token)==ESP_OK && unregisters==calls+2 && !broker(retire)(&token));
    assert(status().submit_error==81 && status().correlation_fault);
    assert(broker(reset_after_deinit)(7) && allocations==frees);
    assert(status().native_terminated && status().submit_error==81 && status().correlation_fault);
    consume_termination(&token);
    send_error=0;assert(broker(register)(7)==ESP_OK && submit(&token)==ESP_OK);
    driver_callback(&callback_info);driver_callback(&callback_info);
    assert(status().duplicate_callbacks==1 && status().quarantined && !broker(retire)(&token));
    assert(broker(reset_after_deinit)(7));consume_termination(&token);
    assert(broker(register)(7)==ESP_OK);
    /* Successful unregister retries only the callback drain suffix. */
    start_on_unregister=true;calls=unregisters;
    assert(broker(unregister)(7)==ESP_ERR_TIMEOUT && status().unregister_written);
    assert(broker(register)(7)==ESP_ERR_INVALID_STATE);
    finish_callback();
    assert(broker(unregister)(7)==ESP_OK && unregisters==calls+1);
    assert(status().generation==7 && status().orphan_callbacks==1);
    assert(broker(reset_after_deinit)(7));
    assert(broker(register)(7)==ESP_OK);
    /* Seed the production counter only to reach the boot-lifetime limit. */
    s_raw_tx.next_identity=UINT32_MAX;
    assert(submit(&token)==ESP_OK && token.identity==UINT32_MAX && status().identity_exhausted);
    driver_callback(&callback_info);retire(&token);
    assert(submit(&next)==ESP_ERR_INVALID_STATE && !next.identity);
    assert(broker(reset_after_deinit)(7) && broker(register)(7)==ESP_OK);
    assert(submit(&next)==ESP_ERR_INVALID_STATE && status().identity_exhausted);
    assert(broker(unregister)(7)==ESP_OK && broker(reset_after_deinit)(7));
    assert(allocations==frees);
    return 0;
}
