
int main(void) {
    const char *stage;
    reset();
    for(uint32_t mask=0;mask<=31;mask++) {
        assert(esp32_mquickjs_wifi_radio_dump_stats(mask,&stage)==ESP_OK && !stage && seen_mask==mask);
    }
    assert(sdk_calls==32 && locked_calls==32);
    assert(esp32_mquickjs_wifi_radio_dump_stats(UINT32_MAX,&stage)==ESP_OK && seen_mask==UINT32_MAX);
    unsigned count=sdk_calls,entered=locked_calls;
    assert(esp32_mquickjs_wifi_radio_dump_stats(32,&stage)==ESP_ERR_INVALID_ARG && !strcmp(stage,"mask"));
    assert(esp32_mquickjs_wifi_radio_dump_stats(0,NULL)==ESP_ERR_INVALID_ARG);
    assert(sdk_calls==count && locked_calls==entered);
    for(unsigned scenario=0;scenario<8;scenario++) {
        reset();
        switch(scenario) {
        case 0:s_radio.driver_owned=false;break;
        case 1:s_radio.storage_configured=false;break;
        case 2:s_radio.lifecycle.identity=7;break;
        case 3:s_radio.operation.identity=9;break;
        case 4:s_radio.fault_stage="failed";break;
        case 5:s_radio.cleanup_stage="pending";break;
        case 6:s_radio.restart_required=true;break;
        case 7:s_radio.driver_state=ESP32_MQUICKJS_WIFI_RADIO_UNINITIALIZED;break;
        }
        assert(esp32_mquickjs_wifi_radio_dump_stats(31,&stage)!=ESP_OK && !strcmp(stage,"admission") && !sdk_calls && !locks);
    }
    reset();s_radio.driver_state=ESP32_MQUICKJS_WIFI_RADIO_STOPPED;sdk_error=91;
    assert(esp32_mquickjs_wifi_radio_dump_stats(3,&stage)==91 && !strcmp(stage,"driver-dump") && sdk_calls==1 && !locks);
    assert(!s_radio.fault_stage && !s_radio.restart_required);
}
