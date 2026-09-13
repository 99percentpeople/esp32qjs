
static int request(int32_t dbm) {
    int32_t actual;
    return esp32_mquickjs_wifi_apply_connection_control(
        ESP32_MQUICKJS_WIFI_CONNECTION_RSSI_THRESHOLD,WIFI_IF_STA,dbm,&actual,&result);
}
int main(void) {
    esp32_mquickjs_wifi_rssi_request_t state;
    reset();assert(!esp32_mquickjs_wifi_radio_rssi_request_status(&state));
    assert(!state.revision && !calls && !writes);
    assert(!esp32_mquickjs_wifi_radio_rssi_request_status(NULL) && !locks);
    assert(request(-75)==ESP_OK && calls==1 && writes==1);
    assert(esp32_mquickjs_wifi_radio_rssi_request_status(&state));
    assert(state.generation==7 && state.revision==1 && state.requested_dbm==-75 && state.error==ESP_OK);
    assert(request(-75)==ESP_OK && calls==2 && writes==2);
    assert(esp32_mquickjs_wifi_radio_rssi_request_status(&state) && state.revision==2);
    fail_at=3;assert(request(-80)==77 && threshold==-80 && writes==3);
    assert(esp32_mquickjs_wifi_radio_rssi_request_status(&state));
    assert(state.revision==3 && state.requested_dbm==-80 && state.error==77 && !s_radio.fault_stage);
    esp32_mquickjs_wifi_rssi_request_t saved=state;
    unsigned previous_calls=calls;
    s_wifi_state.scan_draining=true;
    assert(request(-60)==ESP_ERR_INVALID_STATE && !result.mutation_attempted);
    s_wifi_state.scan_draining=false;
    assert(request(-101)==ESP_ERR_INVALID_ARG && !result.mutation_attempted);
    assert(esp32_mquickjs_wifi_radio_rssi_request_status(&state));
    assert(!memcmp(&saved,&state,sizeof(state)) && calls==previous_calls);
    s_radio.started=false;s_radio.driver_state=ESP32_MQUICKJS_WIFI_RADIO_STOPPED;
    assert(esp32_mquickjs_wifi_radio_rssi_request_status(&state)); /* Same generation, no armed claim. */
    s_radio.driver_owned=false;
    assert(!esp32_mquickjs_wifi_radio_rssi_request_status(&state));
    assert(!memcmp(&saved,&state,sizeof(state)) && calls==previous_calls);
    s_radio.driver_owned=true;++s_radio.generation;
    assert(!esp32_mquickjs_wifi_radio_rssi_request_status(&state));
    assert(!memcmp(&saved,&state,sizeof(state)) && calls==previous_calls);
    /* Recreate the injected helper generation; only an explicit request writes. */
    s_radio.started=true;s_radio.driver_state=ESP32_MQUICKJS_WIFI_RADIO_STARTED;
    s_wifi_application.generation=s_wifi_state.radio_lease.generation=s_ap_lease.generation=s_radio.generation;
    fail_at=0;assert(request(-60)==ESP_OK && calls==previous_calls+1);
    assert(esp32_mquickjs_wifi_radio_rssi_request_status(&state));
    assert(state.generation==8 && state.revision==4 && state.requested_dbm==-60 && !state.error);
    s_rssi_request.revision=UINT32_MAX-1;
    assert(request(10)==ESP_OK && s_rssi_request.revision==UINT32_MAX);
    saved=s_rssi_request;previous_calls=calls;
    assert(request(-100)==ESP_ERR_NO_MEM && !result.mutation_attempted);
    assert(!strcmp(result.stage,"rssi-threshold-identity") && !s_radio.fault_stage);
    assert(calls==previous_calls && !memcmp(&saved,&s_rssi_request,sizeof(saved)));
    ++s_radio.generation;
    s_wifi_application.generation=s_wifi_state.radio_lease.generation=s_ap_lease.generation=s_radio.generation;
    assert(request(-70)==ESP_ERR_NO_MEM && calls==previous_calls);
    assert(!esp32_mquickjs_wifi_radio_rssi_request_status(&state) && state.revision==UINT32_MAX);
    assert(!locks && !critical && !helper_locks);
    return 0;
}
