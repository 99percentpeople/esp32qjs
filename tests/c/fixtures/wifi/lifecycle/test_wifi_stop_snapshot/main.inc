
static esp32_mquickjs_wifi_radio_stop_snapshot_t read_snapshot(void) {
    esp32_mquickjs_wifi_radio_stop_snapshot_t result;
    esp32_mquickjs_wifi_radio_stop_snapshot(&result);return result;
}
static void observe(void) {
    wifi_radio_operation_lock();wifi_radio_capture_stop_snapshot_locked();wifi_radio_operation_unlock();
}
static void reset_observation(void) {
    setup();memset(&s_stop_snapshot,0,sizeof(s_stop_snapshot));
    s_radio.event_identity=51;calls=writes=fail_at=0;
}
int main(void) {
    reset_observation();
    esp32_mquickjs_wifi_radio_stop_snapshot_t snapshot=read_snapshot();
    assert(!snapshot.stop_identity && snapshot.step==ESP32_MQUICKJS_WIFI_STOP_SNAPSHOT_NONE);
    observe();snapshot=read_snapshot();unsigned sdk_calls=calls;
    assert(sdk_calls==9 && !writes && !native_stops && !native_starts && !allocations);
    assert(snapshot.inactive_time[0]==17 && !snapshot.inactive_time[1]);
    assert(snapshot.generation==7 && snapshot.stop_identity==51 && snapshot.error==ESP_OK);
    assert(snapshot.step==ESP32_MQUICKJS_WIFI_STOP_SNAPSHOT_COMPLETE && snapshot.tx_power==52);
    assert(snapshot.mode==WIFI_MODE_STA && snapshot.band==WIFI_BAND_2G && snapshot.primary==1 && snapshot.secondary==WIFI_SECOND_CHAN_NONE);
    assert(s_radio.started && !s_radio.fault_stage && !s_radio.cleanup_stage);
    s_radio.started=false;s_radio.generation=8;native_tx_power=80;native_home=11;
    esp32_mquickjs_wifi_radio_stop_snapshot_t historical=read_snapshot();
    assert(historical.generation==snapshot.generation && historical.stop_identity==snapshot.stop_identity &&
        historical.tx_power==snapshot.tx_power && historical.primary==snapshot.primary &&
        historical.error==ESP_OK && calls==sdk_calls); /* Historical, not a current getter. */
    esp32_mquickjs_wifi_radio_stop_snapshot(NULL);assert(calls==sdk_calls);

    for(unsigned failure=1;failure<=sdk_calls;++failure) {
        reset_observation();fail_at=failure;observe();snapshot=read_snapshot();
        if(failure==1) {
            /* Scan history has its own error; native STOP must still proceed
             * with valid power/channel/inactive observations. */
            assert(snapshot.error==ESP_OK && !s_scan_parameters.known && s_scan_parameters.error==77);
            assert(calls==sdk_calls && !writes && !s_radio.fault_stage);
            continue;
        }
        assert(snapshot.error==77 && snapshot.stop_identity==51 && snapshot.generation==7);
        assert(snapshot.step!=ESP32_MQUICKJS_WIFI_STOP_SNAPSHOT_COMPLETE);
        assert(!snapshot.tx_power && !snapshot.primary && !snapshot.band_mode && !snapshot.band && !snapshot.secondary);
        assert(!snapshot.inactive_time[0] && !snapshot.inactive_time[1]);
        assert(calls==failure && !writes && !s_radio.fault_stage && !s_radio.cleanup_stage && s_radio.started);
    }
    reset_observation();native_tx_power=0;observe();snapshot=read_snapshot();
    assert(snapshot.error==ESP_ERR_INVALID_RESPONSE && snapshot.step==ESP32_MQUICKJS_WIFI_STOP_SNAPSHOT_POWER && !snapshot.tx_power);
    reset_observation();inactive[0]=2;observe();snapshot=read_snapshot();
    assert(snapshot.error==ESP_ERR_INVALID_RESPONSE && snapshot.step==ESP32_MQUICKJS_WIFI_STOP_SNAPSHOT_STA_INACTIVE);
    assert(!snapshot.inactive_time[0] && !snapshot.tx_power);
    reset_observation();s_radio.effective_mode=native_mode=WIFI_MODE_APSTA;
    observe();snapshot=read_snapshot();
    assert(snapshot.error==ESP_OK && snapshot.inactive_time[0]==17 && snapshot.inactive_time[1]==601 && calls==10);
    reset_observation();native_home=11;observe();snapshot=read_snapshot();
    assert(snapshot.error==ESP_ERR_INVALID_RESPONSE && snapshot.step==ESP32_MQUICKJS_WIFI_STOP_SNAPSHOT_VERIFY && !snapshot.primary);
    reset_observation();native_secondary=native_home_secondary=(wifi_second_chan_t)99;observe();snapshot=read_snapshot();
    assert(snapshot.error==ESP_ERR_INVALID_RESPONSE && !snapshot.secondary);
    reset_observation();s_radio.started=false;observe();snapshot=read_snapshot();
    assert(snapshot.error==ESP_ERR_INVALID_STATE && snapshot.step==ESP32_MQUICKJS_WIFI_STOP_SNAPSHOT_ADMISSION && !calls);
    reset_observation();s_radio.fault_stage="uncertain";observe();snapshot=read_snapshot();
    assert(snapshot.error==ESP_ERR_INVALID_STATE && !calls && !strcmp(s_radio.fault_stage,"uncertain"));
#if CONFIG_SOC_WIFI_SUPPORT_5G
    reset_observation();native_band_mode=WIFI_BAND_MODE_5G_ONLY;native_band=WIFI_BAND_5G;native_channel=native_home=36;
    observe();snapshot=read_snapshot();
    assert(snapshot.error==ESP_OK && snapshot.band==WIFI_BAND_5G && snapshot.band_mode==WIFI_BAND_MODE_5G_ONLY && snapshot.primary==36);
#endif
    assert(!locks && !critical && !helper_locks && !allocation);
    return 0;
}
