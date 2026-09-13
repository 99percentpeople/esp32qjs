int main(void) {
    (void)start_callback;(void)finish_callback;
    memset(bytes+4,0x34,6);memset(bytes+10,0x12,6);
    memcpy(destination,bytes+4,6);memcpy(source_address,bytes+10,6);
    callback_info=(esp_80211_tx_info_t){.des_addr=destination,.src_addr=source_address,
        .ifidx=WIFI_IF_STA,.tx_status=WIFI_SEND_SUCCESS};
    esp32_mquickjs_wifi_radio_lease_t lease={0};acquire(&lease);
    token_t tokens[3]={{0}};void *identities[3];
    for(unsigned i=0;i<3;++i){assert(!send_frame(&lease,&tokens[i],true));identities[i]=last_descriptor;}
    assert(s_radio.leases[0].raw_tx_identity==tokens[0].identity && s_radio.leases[0].fixed_channel);
    esp32_mquickjs_wifi_radio_release(&lease);assert(lease.acquired);
    esp_80211_tx_info_t info;token_t stale=tokens[0];
    esp32qjs_raw_tx_info(identities[0],&info);assert(radio(retire)(&lease,&tokens[0]));
    assert(s_radio.leases[0].raw_tx_identity==tokens[1].identity && s_radio.leases[0].fixed_channel);
    assert(!radio(retire)(&lease,&stale));
    esp32qjs_raw_tx_info(identities[2],&info);assert(radio(retire)(&lease,&tokens[2]));
    esp32_mquickjs_wifi_radio_release(&lease);assert(lease.acquired && s_radio.leases[0].fixed_channel);
    esp32qjs_raw_tx_info(identities[1],&info);assert(radio(retire)(&lease,&tokens[1]));
    assert(!s_radio.leases[0].raw_tx_identity && !s_radio.leases[0].fixed_channel);
    esp32_mquickjs_wifi_radio_release(&lease);assert(!lease.acquired && allocations==frees);
    return 0;
}
