
int main(void) {
    for(unsigned failure=0;failure<2;++failure) {
        setup();rssi_calls=0;
        /* Establish an explicit request through the real stopped owner gate.
         * State transitions are SDK fixture inputs, not physical STOP proof. */
        s_radio.lifecycle.identity=0;s_radio.started=s_radio.stop_required=false;
        s_radio.driver_state=ESP32_MQUICKJS_WIFI_RADIO_STOPPED;
        int32_t actual;fail_at=failure ? 1 : 0;
        assert(esp32_mquickjs_wifi_radio_connection_control(NULL,NULL,NULL,
            ESP32_MQUICKJS_WIFI_CONNECTION_RSSI_THRESHOLD,WIFI_IF_STA,-75,&actual,&result)==(failure ? 77 : ESP_OK));
        assert(rssi_calls==1);esp32_mquickjs_wifi_rssi_request_t saved=s_rssi_request;
        s_radio.lifecycle.identity=41;s_radio.started=s_radio.stop_required=native_running=true;
        s_radio.driver_state=ESP32_MQUICKJS_WIFI_RADIO_STARTED;calls=writes=fail_at=0;
        wifi_radio_operation_lock();
        assert(wifi_radio_restart_configs_capture_locked(&token,WIFI_MODE_STA)==ESP_OK);
        new_driver();threshold=12345; /* Arbitrary boundary sentinel, not an SDK default. */
        assert(wifi_radio_restart_configs_replay_locked(&token)==ESP_OK);
        assert(wifi_radio_restart_configs_pre_start_locked(&token,WIFI_MODE_STA)==ESP_OK);
        s_radio.started=s_radio.stop_required=native_running=true;
        s_radio.driver_state=ESP32_MQUICKJS_WIFI_RADIO_STARTED;
        assert(wifi_radio_restart_configs_post_start_locked(&token)==ESP_OK);
        assert(wifi_radio_restart_configs_commit_locked(&token,WIFI_MODE_STA)==ESP_OK);
        wipe();wifi_radio_operation_unlock();
        esp32_mquickjs_wifi_rssi_request_t observed;
        assert(!esp32_mquickjs_wifi_radio_rssi_request_status(&observed));
        assert(!memcmp(&observed,&saved,sizeof(saved)) && rssi_calls==1 && threshold==12345);
    }
    assert(!allocation && !locks && !critical && !helper_locks);
    return 0;
}
