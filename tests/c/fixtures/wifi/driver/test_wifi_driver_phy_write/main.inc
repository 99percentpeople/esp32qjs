
#undef calloc
#undef free
static esp32_mquickjs_wifi_phy_readback_t request,actual;
static void reset(void) {
    assert(!depth && !critical);
    memset(&s_radio,0,sizeof(s_radio));memset(&request,0,sizeof(request));memset(&result,0,sizeof(result));
    s_radio.driver_owned=s_radio.storage_configured=true;s_radio.driver_state=ESP32_MQUICKJS_WIFI_RADIO_STOPPED;
    s_radio.effective_mode=WIFI_MODE_STA;s_radio.storage=WIFI_STORAGE_RAM;
    native_band=WIFI_BAND_MODE_2G_ONLY;s_tx_rate_lease.identity=0;
    for(int i=0;i<2;++i) {
        native_protocols[i]=(wifi_protocols_t){.ghz_2g=WIFI_PROTOCOL_11B|WIFI_PROTOCOL_11G|WIFI_PROTOCOL_11N,
                                              .ghz_5g=WIFI_PROTOCOL_11A|WIFI_PROTOCOL_11N};
        native_bandwidths[i]=(wifi_bandwidths_t){.ghz_2g=WIFI_BW20,.ghz_5g=WIFI_BW20};
    }
    request.bands=1;request.bandwidths.ghz_2g=WIFI_BW40;
    calls=writes=fail_at=rollback_fail=0;
}
static int set(esp32_mquickjs_wifi_phy_query_t query) {
    return esp32_mquickjs_wifi_radio_write_phy(WIFI_IF_STA,query,&request,&actual,&result);
}
int main(void) {
    reset();assert(set(ESP32_MQUICKJS_WIFI_PHY_BANDWIDTHS)==ESP_OK && actual.bandwidths.ghz_2g==WIFI_BW40);
    assert(actual.bands==1 && !actual.bandwidths.ghz_5g && result.mutation_attempted && !result.rollback_attempted);
    assert(native_bandwidths[1].ghz_2g==WIFI_BW20);int total=calls;
    for(int nth=1;nth<=total;++nth) {
        reset();fail_at=nth;assert(set(ESP32_MQUICKJS_WIFI_PHY_BANDWIDTHS)==-77 && result.error==-77);
        assert(!depth && !critical && !actual.bands && s_radio.configuration.error==-77);
        if(result.mutation_attempted) {
            assert(result.rollback_complete && native_bandwidths[0].ghz_2g==WIFI_BW20 && !s_radio.fault_stage);
        } else assert(!writes && !result.rollback_attempted);
    }
    reset();fail_at=total;assert(set(ESP32_MQUICKJS_WIFI_PHY_BANDWIDTHS)==-77);int rollback_calls=calls-total;
    for(int nth=1;nth<=rollback_calls;++nth) {
        reset();fail_at=total;rollback_fail=nth;assert(set(ESP32_MQUICKJS_WIFI_PHY_BANDWIDTHS)==-77);
        assert(result.rollback_error==-88 && !result.rollback_complete && s_radio.fault_stage && s_radio.cleanup_stage);
        int before=calls;fail_at=0;assert(set(ESP32_MQUICKJS_WIFI_PHY_BANDWIDTHS)==ESP_ERR_INVALID_STATE && calls==before);
    }
    reset();s_radio.storage=WIFI_STORAGE_FLASH;fail_at=total;
    assert(set(ESP32_MQUICKJS_WIFI_PHY_BANDWIDTHS)==-77 && result.rollback_complete && result.persistent_mutation_possible && s_radio.fault_stage);
    reset();s_radio.leases[1].identity=9;assert(set(ESP32_MQUICKJS_WIFI_PHY_BANDWIDTHS)==ESP_ERR_INVALID_STATE && !calls);
    reset();s_radio.lifecycle.identity=9;assert(set(ESP32_MQUICKJS_WIFI_PHY_BANDWIDTHS)==ESP_ERR_INVALID_STATE && !calls);
    reset();s_radio.operation.identity=9;assert(set(ESP32_MQUICKJS_WIFI_PHY_BANDWIDTHS)==ESP_ERR_INVALID_STATE && !calls);
    reset();s_tx_rate_lease.identity=9;assert(set(ESP32_MQUICKJS_WIFI_PHY_BANDWIDTHS)==ESP_ERR_INVALID_STATE && !calls);
    reset();s_radio.wake_locks=1;assert(set(ESP32_MQUICKJS_WIFI_PHY_BANDWIDTHS)==ESP_ERR_INVALID_STATE && !calls);
    reset();s_radio.started=true;assert(set(ESP32_MQUICKJS_WIFI_PHY_BANDWIDTHS)==ESP_ERR_INVALID_STATE && !calls);
    reset();s_radio.driver_owned=false;assert(set(ESP32_MQUICKJS_WIFI_PHY_BANDWIDTHS)==ESP_ERR_WIFI_NOT_INIT && !calls);
    reset();request.bandwidths.ghz_2g=99;assert(set(ESP32_MQUICKJS_WIFI_PHY_BANDWIDTHS)==ESP_ERR_INVALID_ARG && !writes);
    reset();request.protocols.ghz_2g=WIFI_PROTOCOL_11N;assert(set(ESP32_MQUICKJS_WIFI_PHY_PROTOCOLS)==ESP_ERR_INVALID_ARG && !writes);
    reset();request.protocols.ghz_2g=WIFI_PROTOCOL_11B;
    assert(set(ESP32_MQUICKJS_WIFI_PHY_PROTOCOLS)==ESP_OK && actual.protocols.ghz_2g==WIFI_PROTOCOL_11B);
    reset();native_bandwidths[0].ghz_2g=WIFI_BW40;request.protocols.ghz_2g=WIFI_PROTOCOL_11B;
    assert(set(ESP32_MQUICKJS_WIFI_PHY_PROTOCOLS)==ESP_ERR_INVALID_ARG && !writes);
    reset();native_bandwidths[0].ghz_2g=99;assert(set(ESP32_MQUICKJS_WIFI_PHY_BANDWIDTHS)==ESP_ERR_INVALID_RESPONSE && !writes);
    reset();request.bands=2;request.protocols.ghz_5g=WIFI_PROTOCOL_11A;
#if CONFIG_SOC_WIFI_SUPPORT_5G
    assert(set(ESP32_MQUICKJS_WIFI_PHY_PROTOCOLS)==ESP_ERR_INVALID_STATE && !writes);
    reset();native_band=WIFI_BAND_MODE_AUTO;request.protocols.ghz_2g=WIFI_PROTOCOL_11B;
    assert(set(ESP32_MQUICKJS_WIFI_PHY_PROTOCOL)==ESP_ERR_NOT_SUPPORTED && !writes);
    assert(set(ESP32_MQUICKJS_WIFI_PHY_PROTOCOLS)==ESP_OK && actual.bands==3 && actual.protocols.ghz_5g==(WIFI_PROTOCOL_11A|WIFI_PROTOCOL_11N));
    reset();native_band=WIFI_BAND_MODE_5G_ONLY;request.protocols.ghz_2g=WIFI_PROTOCOL_11A;
    assert(set(ESP32_MQUICKJS_WIFI_PHY_PROTOCOL)==ESP_OK && actual.bands==1 && actual.protocols.ghz_2g==WIFI_PROTOCOL_11A);
#else
    assert(set(ESP32_MQUICKJS_WIFI_PHY_PROTOCOLS)==ESP_ERR_NOT_SUPPORTED && !writes);
#endif
    reset();s_radio.effective_mode=WIFI_MODE_AP;
    int err=esp32_mquickjs_wifi_radio_write_phy(WIFI_IF_AP,ESP32_MQUICKJS_WIFI_PHY_BANDWIDTHS,&request,&actual,&result);
#if CONFIG_ESP_WIFI_SOFTAP_SUPPORT
    assert(err==ESP_OK && native_bandwidths[1].ghz_2g==WIFI_BW40 && native_bandwidths[0].ghz_2g==WIFI_BW20);
#else
    assert(err==ESP_ERR_NOT_SUPPORTED && !writes);
#endif
    return 0;
}
