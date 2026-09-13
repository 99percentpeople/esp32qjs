
int main(void) {
    int32_t actual;
    band_reset();associated=true;
    assert(esp32_mquickjs_wifi_apply_band(true,WIFI_BAND_MODE_2G_ONLY,&actual,&result)==ESP_OK && !writes && actual==WIFI_BAND_MODE_2G_ONLY);
    assert(esp32_mquickjs_wifi_apply_band(false,WIFI_BAND_2G,&actual,&result)==ESP_ERR_NOT_SUPPORTED && !writes);
#if CONFIG_SOC_WIFI_SUPPORT_5G
    band_reset();assert(esp32_mquickjs_wifi_apply_band(true,WIFI_BAND_MODE_AUTO,&actual,&result)==ESP_OK && actual==WIFI_BAND_MODE_AUTO);
    assert(esp32_mquickjs_wifi_apply_band(false,WIFI_BAND_5G,&actual,&result)==ESP_OK && actual==WIFI_BAND_5G && native_channel==36);
    band_reset();native_band_mode=WIFI_BAND_MODE_AUTO;assert(esp32_mquickjs_wifi_apply_band(false,WIFI_BAND_5G,&actual,&result)==ESP_OK);unsigned total=calls;
    for(unsigned nth=1;nth<=total;++nth) {
        band_reset();native_band_mode=WIFI_BAND_MODE_AUTO;fail_at=nth;
        assert(esp32_mquickjs_wifi_apply_band(false,WIFI_BAND_5G,&actual,&result)==77 && !actual);
        assert(!result.rollback_attempted && !locks && !critical && !helper_locks);
        if(result.mutation_attempted) {
            assert(s_radio.fault_error==77 && writes==1);
            unsigned before=calls;fail_at=0;assert(esp32_mquickjs_wifi_apply_band(false,WIFI_BAND_5G,&actual,&result)==ESP_ERR_INVALID_STATE && calls==before);
        } else assert(!s_radio.fault_stage && !writes);
    }
    band_reset();associated=true;assert(esp32_mquickjs_wifi_apply_band(true,WIFI_BAND_MODE_AUTO,&actual,&result)==ESP_ERR_INVALID_STATE && !writes);
    band_reset();native_band_mode=WIFI_BAND_MODE_AUTO;corrupt_channel=true;
    assert(esp32_mquickjs_wifi_apply_band(false,WIFI_BAND_5G,&actual,&result)==ESP_ERR_INVALID_RESPONSE && s_radio.fault_stage);
    band_reset();native_band_mode=WIFI_BAND_MODE_AUTO;deny_regulatory=true;
    assert(esp32_mquickjs_wifi_apply_band(false,WIFI_BAND_5G,&actual,&result)==ESP_ERR_NOT_ALLOWED && s_radio.fault_stage);
#else
    band_reset();assert(esp32_mquickjs_wifi_apply_band(true,WIFI_BAND_MODE_AUTO,&actual,&result)==ESP_ERR_NOT_SUPPORTED && !calls);
    assert(esp32_mquickjs_wifi_apply_band(false,WIFI_BAND_5G,&actual,&result)==ESP_ERR_NOT_SUPPORTED && !calls);
#endif
    band_reset();s_radio.effective_mode=WIFI_MODE_APSTA;assert(esp32_mquickjs_wifi_apply_band(true,WIFI_BAND_MODE_2G_ONLY,&actual,&result)==ESP_ERR_INVALID_STATE && !calls);
    band_reset();s_radio.leases[1].fixed_channel=true;assert(esp32_mquickjs_wifi_apply_band(true,WIFI_BAND_MODE_2G_ONLY,&actual,&result)==ESP_ERR_INVALID_STATE && !calls);
    band_reset();s_wifi_state.radio_lease.generation++;assert(esp32_mquickjs_wifi_apply_band(true,WIFI_BAND_MODE_2G_ONLY,&actual,&result)==ESP_ERR_INVALID_STATE && !calls);
    band_reset();s_wifi_state.scan_draining=true;assert(esp32_mquickjs_wifi_apply_band(true,WIFI_BAND_MODE_2G_ONLY,&actual,&result)==ESP_ERR_INVALID_STATE && !calls);
    band_reset();corrupt_band=true;assert(esp32_mquickjs_wifi_apply_band(true,WIFI_BAND_MODE_2G_ONLY,&actual,&result)==ESP_ERR_INVALID_RESPONSE && !writes);
    return 0;
}
