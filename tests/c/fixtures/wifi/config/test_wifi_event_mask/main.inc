
int main(void) {
    uint32_t actual;
#define GET() esp32_mquickjs_wifi_radio_event_mask(false,0,&actual,&result)
#define SET(value) esp32_mquickjs_wifi_radio_event_mask(true,value,&actual,&result)
    mask_reset();s_radio.configuration.error=123;assert(GET()==ESP_OK && actual==1 && calls==1 && !writes && s_radio.configuration.error==123);
    assert(SET(1)==ESP_OK && !result.mutation_attempted && !writes);
    assert(SET(0)==ESP_OK && actual==0 && writes==1 && !unsafe_writes);
    for(unsigned bit=1;bit<32;++bit) {unsigned before=calls;assert(SET(UINT32_C(1)<<bit)==ESP_ERR_INVALID_ARG && calls==before);}
    unsigned before=calls;assert(SET(UINT32_MAX)==ESP_ERR_INVALID_ARG && calls==before);
    mask_reset();assert(SET(0)==ESP_OK);unsigned total=calls;
    for(unsigned nth=1;nth<=total;++nth) {
        mask_reset();fail_at=nth;assert(SET(0)==77 && !actual && result.error==77 && !unsafe_writes);
        if(result.mutation_attempted)assert(result.rollback_complete && native_mask==1 && !s_radio.fault_stage);
        else assert(!writes && !result.rollback_attempted);
    }
    mask_reset();fail_at=total;assert(SET(0)==77);unsigned suffix=calls-total;
    for(unsigned nth=1;nth<=suffix;++nth) {
        mask_reset();fail_at=total;rollback_fail=nth;assert(SET(0)==77 && result.rollback_error==88);
        assert(s_radio.fault_error==77 && !result.rollback_complete && s_radio.cleanup_error==88 && !unsafe_writes);
        before=calls;fail_at=0;assert(SET(0)==ESP_ERR_INVALID_STATE && calls==before);
    }
    mask_reset();corrupt_mask_readback=true;assert(SET(0)==ESP_ERR_INVALID_RESPONSE && result.rollback_complete && native_mask==1);
    /* A persistent readback mismatch must retain both the first error and the
     * failed rollback, even though the injected setter restored the bits. */
    mask_reset();corrupt_mask_readback=corrupt_mask_rollback=true;
    assert(SET(0)==ESP_ERR_INVALID_RESPONSE && result.rollback_attempted && !result.rollback_complete);
    assert(result.rollback_error==ESP_ERR_INVALID_RESPONSE && native_mask==1);
    assert(s_radio.fault_error==ESP_ERR_INVALID_RESPONSE && s_radio.cleanup_error==ESP_ERR_INVALID_RESPONSE);
    assert(!strcmp(result.stage,"event-mask-readback") && !strcmp(result.rollback_stage,"rollback-event-mask-readback"));
    before=calls;assert(SET(0)==ESP_ERR_INVALID_STATE && calls==before && !unsafe_writes);
    mask_reset();native_mask=UINT32_MAX;assert(GET()==ESP_OK && actual==UINT32_MAX);
    assert(SET(0)==ESP_OK && native_mask==0 && !unsafe_writes);
    mask_reset();native_mask=UINT32_MAX;fail_at=2;assert(SET(0)==77 && !result.rollback_attempted && writes==1 && !unsafe_writes && s_radio.fault_error==77);
    mask_reset();s_radio.started=false;s_radio.driver_state=ESP32_MQUICKJS_WIFI_RADIO_STOPPED;
    assert(SET(0)==ESP_OK); /* Interface mode and RF owners do not affect this observation-only mask. */
    mask_reset();s_wifi_state.scan_in_progress=true;s_radio.wake_locks=1;s_radio.promiscuous_claimed=true;s_tx_rate_lease.identity=99;
    s_radio.leases[3]=(wifi_radio_live_lease_t){21,ESP32_MQUICKJS_WIFI_RADIO_CLIENT_RAW_TX};
    assert(SET(0)==ESP_OK && !unsafe_writes);
    mask_reset();s_radio.operation.identity=1;assert(SET(0)==ESP_ERR_INVALID_STATE && !calls);s_radio.operation.identity=0;
    s_radio.lifecycle.identity=1;assert(GET()==ESP_ERR_INVALID_STATE && !calls);s_radio.lifecycle.identity=0;
    s_radio.driver_owned=false;assert(GET()==ESP_ERR_WIFI_NOT_INIT && !calls);
    assert(!locks && !critical && !helper_locks);return 0;
}
