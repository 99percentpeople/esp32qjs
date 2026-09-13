
int main(void) {
    esp_wps_config_t config={.wps_type=WPS_TYPE_PIN};
    for(unsigned i=1;i<=4;i++) {
        reset();fail_nth=i;
        assert(wifi_station_wps_init(&config)==ESP_ERR_NO_MEM);
        assert(record.status.error==ESP_ERR_NO_MEM && record.status.terminal_seen);
        assert(record.status.error_stage==(i<=2?ESP32QJS_WPS_STAGE_CONTEXT:ESP32QJS_WPS_STAGE_CALLBACKS));
        retire_fixture_sdk();
    }
    const int stages[]={ESP32QJS_WPS_STAGE_MAC,ESP32QJS_WPS_STAGE_DEVICE,ESP32QJS_WPS_STAGE_PIN,
        ESP32QJS_WPS_STAGE_CALLBACKS,ESP32QJS_WPS_STAGE_PROBE_IE,ESP32QJS_WPS_STAGE_ASSOC_IE};
    for(unsigned i=0;i<6;i++) {
        reset();int expected=601+i;
        if(i==0)mac_error=expected;if(i==1)device_error=expected;if(i==2)pin_error=expected;
        if(i==3)callback_error=expected;if(i==4)appie_error=expected;
        if(i==5){assoc_oom=1;expected=ESP_ERR_NO_MEM;}
        assert(wifi_station_wps_init(&config)==expected);
        assert(record.status.error==expected && record.status.error_stage==stages[i]);
        assert(esp32qjs_wps_record_error(999,ESP32QJS_WPS_STAGE_CONNECT)==999);
        assert(record.status.error==expected && !record.status.pin_available);
        retire_fixture_sdk();
    }
    reset();assert(wifi_station_wps_init(&config)==ESP_OK);
    gWpsSm->discover_ssid_cnt=1;gWpsSm->creds[0].ssid_len=3;
    config_error=701;wifi_wps_scan_done_body(NULL,0);
    assert(config_calls==1 && !connect_calls && record.status.error==701 && record.status.error_stage==ESP32QJS_WPS_STAGE_CONFIG);
    retire_fixture_sdk();
    reset();assert(wifi_station_wps_init(&config)==ESP_OK);
    gWpsSm->discover_ssid_cnt=1;connect_error=702;wifi_wps_scan_done_body(NULL,0);
    assert(config_calls==1 && connect_calls==1 && !timers && record.status.error_stage==ESP32QJS_WPS_STAGE_CONNECT);
    retire_fixture_sdk();
    reset();assert(wifi_station_wps_init(&config)==ESP_OK);
    wifi_wps_scan_done_body(NULL,9);
    assert(!config_calls && !connect_calls && record.status.error==9 && record.status.error_stage==ESP32QJS_WPS_STAGE_SCAN_DONE);
    retire_fixture_sdk();
    for(unsigned i=0;i<4;i++) {
        reset();assert(wifi_station_wps_init(&config)==ESP_OK);
        if(i==0)associated_error=801;if(i==1)tx_error=802;if(i==2)tx_oom=true;
        int expected=i==0?801:i==1?802:i==2?ESP_ERR_NO_MEM:ESP_ERR_INVALID_SIZE;
        assert(wps_send_eapol_frame(0,"secret",i==3?65536:6)==expected);
        assert(record.status.error==expected && record.status.terminal_seen);
        assert(tx_frees==(i<2?1:0));retire_fixture_sdk();
    }
    uint8_t rsn[]={48,0};struct wps_scan_ie scan={.rsn=rsn};
    parse_error=-1;assert(!is_ap_supports_sae(&scan));
    parse_error=0;assert(is_ap_supports_sae(&scan));
    return 0;
}
