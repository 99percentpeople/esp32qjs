int main(void) {
    osi[21]=(void *)lock;osi[22]=(void *)unlock;
    g_ic[4]=(u32)station;g_ic[5]=(u32)ap;
    g_ic[0x1d0/4]=(u32)completed;
    channel=4;
    const u8 *frames[]={reported_deauth,reported_probe};
    const unsigned lengths[]={sizeof(reported_deauth),sizeof(reported_probe)};
    for(unsigned frame=0;frame<2;frame++) {
        expected_frame=frames[frame];expected_length=lengths[frame];
        for(expected_sequence=0;expected_sequence<2;expected_sequence++) {
            esp32_mquickjs_wifi_raw_tx_validation_policy_t policy={
                .interface=ESP32_MQUICKJS_WIFI_RAW_TX_STATION,
                .driver_sequence=expected_sequence!=0};
            esp32_mquickjs_wifi_raw_tx_validated_frame_t validated;
            CHECK(esp32_mquickjs_wifi_raw_tx_validate(expected_frame,expected_length,&policy,&validated)
                ==ESP32_MQUICKJS_WIFI_RAW_TX_VALID,10);
            CHECK(ieee80211_raw_frame_sanity_check(0,expected_frame,expected_length,expected_sequence)==0,11);
            for(completion_prefix=0;completion_prefix<=8;completion_prefix+=8) {
                for(completion_status=0;completion_status<256;completion_status++) {
                    CHECK(esp_wifi_80211_tx(0,expected_frame,expected_length,expected_sequence)==0,12);
                    CHECK(!metadata_failure,20+metadata_failure);
                }
            }
        }
    }
    const unsigned expected_completions=2*2*2*256;
    CHECK(posted==expected_completions && allocated_hooks==expected_completions &&
        completed_hooks==expected_completions && public_callbacks==expected_completions,30);
    CHECK(locks>0 && locks==unlocks,31);
    return 0;
}
