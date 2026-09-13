
int main(void){
    reset();esp32_mquickjs_wifi_twt_control_t value={.config={true,false}};uint32_t generation;
    assert(esp32_mquickjs_wifi_apply_twt_control(ESP32_MQUICKJS_WIFI_TWT_WRITE_CONFIG,&value,&generation,&result)==ESP_OK);
    assert(s_twt_policy.known && generation==7 && native_twt_policy.post_wakeup_event);
    s_radio.leases[3]=(wifi_radio_live_lease_t){88,ESP32_MQUICKJS_WIFI_RADIO_CLIENT_TWT};
    unsigned before=calls;
    assert(esp32_mquickjs_wifi_apply_twt_control(ESP32_MQUICKJS_WIFI_TWT_WRITE_CONFIG,&value,&generation,&result)==ESP_ERR_INVALID_STATE && calls==before);
    individual[0].token.identity=99;
    individual[0].lease=(esp32_mquickjs_wifi_radio_lease_t){7,88,ESP32_MQUICKJS_WIFI_RADIO_CLIENT_TWT,true};
    assert(esp32_mquickjs_wifi_apply_twt_control(ESP32_MQUICKJS_WIFI_TWT_WRITE_CONFIG,&value,&generation,&result)==ESP_OK);
    individual[0].lease.generation=6;before=calls;
    assert(esp32_mquickjs_wifi_apply_twt_control(ESP32_MQUICKJS_WIFI_TWT_WRITE_CONFIG,&value,&generation,&result)==ESP_ERR_INVALID_STATE && calls==before);
    reset();s_radio.leases[0]=(wifi_radio_live_lease_t){88,ESP32_MQUICKJS_WIFI_RADIO_CLIENT_CSI};
    s_wifi_application.identity=88;s_wifi_application.acquired=false;before=calls;
    assert(esp32_mquickjs_wifi_apply_twt_control(ESP32_MQUICKJS_WIFI_TWT_WRITE_CONFIG,&value,&generation,&result)==ESP_ERR_INVALID_STATE && calls==before);
    reset();memset(&s_twt_policy,0,sizeof(s_twt_policy));s_wifi_state.radio_lease.identity++;before=calls;
    assert(esp32_mquickjs_wifi_apply_twt_control(ESP32_MQUICKJS_WIFI_TWT_WRITE_OFFSET,&value,&generation,&result)==ESP_ERR_INVALID_STATE && calls==before);
    reset();value.offset_us=102401;
    assert(esp32_mquickjs_wifi_apply_twt_control(ESP32_MQUICKJS_WIFI_TWT_WRITE_OFFSET,&value,&generation,&result)==ESP_ERR_INVALID_ARG && !calls);
    value.offset_us=102400;
    assert(esp32_mquickjs_wifi_apply_twt_control(ESP32_MQUICKJS_WIFI_TWT_WRITE_OFFSET,&value,&generation,&result)==ESP_OK);
    assert(esp32_mquickjs_wifi_radio_twt_control(NULL,NULL,NULL,ESP32_MQUICKJS_WIFI_TWT_READ_FLOWS,&value,&generation,&result)==ESP_OK && value.flow_bitmap==129);
    fail_at=calls+1;
    assert(esp32_mquickjs_wifi_apply_twt_control(ESP32_MQUICKJS_WIFI_TWT_WRITE_CONFIG,&value,&generation,&result)==77 && s_radio.fault_stage);
    return 0;
}
