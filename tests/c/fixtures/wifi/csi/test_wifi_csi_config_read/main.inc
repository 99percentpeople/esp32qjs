
int main(void){
    reset();esp32_mquickjs_wifi_radio_lease_t lease={7,20,ESP32_MQUICKJS_WIFI_RADIO_CLIENT_CSI,true};
    s_radio.leases[2]=(wifi_radio_live_lease_t){20,lease.client};
    wifi_csi_config_t output;uint32_t generation=99;
    int error=esp32_mquickjs_wifi_radio_read_csi_config(&lease,&output,&generation);
#if CONFIG_SOC_WIFI_HE_SUPPORT
    assert(error==ESP_OK && generation==7 && calls==1);
    fail_at=calls+1;assert(esp32_mquickjs_wifi_radio_read_csi_config(&lease,&output,&generation)==77 && !generation);
#else
    assert(error==ESP_ERR_NOT_SUPPORTED && !generation && !calls);
#endif
    unsigned before=calls;lease.identity++;
    assert(esp32_mquickjs_wifi_radio_read_csi_config(&lease,&output,&generation)==ESP_ERR_INVALID_STATE && calls==before);
    lease.identity--;s_radio.lifecycle.identity=50;
    assert(esp32_mquickjs_wifi_radio_read_csi_config(&lease,&output,&generation)==ESP_ERR_INVALID_STATE && calls==before);
    s_radio.lifecycle.identity=0;s_radio.started=false;
    assert(esp32_mquickjs_wifi_radio_read_csi_config(&lease,&output,&generation)==ESP_ERR_INVALID_STATE && calls==before);
    return 0;
}
