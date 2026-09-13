
int main(void) {
    const unsigned channels[]={0,6,36,255};
    esp32_mquickjs_wifi_radio_lease_t owners[3]={{1,true},{2,true},{3,true}};
    esp_wps_config_t config=0;
    for(unsigned support=0;support<2;++support)for(unsigned i=0;i<4;++i) {
        assert(!s_wps_ap_radio && !locked && !critical);
        primary=channels[i];allow_five=support;s_radio.generation=7;s_radio.next_operation_identity=11;
        prepared=started=closed=released=regulatory=0;
        esp32_mquickjs_wifi_radio_operation_t token={0};
        esp32_mquickjs_wifi_wps_ap_radio_status_t status;
        bool accepted=primary==6 || (support && primary==36);
        assert(esp32_mquickjs_wifi_radio_wps_ap_begin(owners,&config,&token,&status)==(accepted?ESP_OK:ESP_ERR_NOT_SUPPORTED));
        assert(regulatory==1 && !locked && !critical);
        if(accepted) {
            assert(token.identity==11 && token.generation==7 && token.lease_identity==3);
            assert(prepared==1 && started==1 && !closed && !released && s_wps_ap_radio);
            free(s_wps_ap_radio);s_wps_ap_radio=NULL; /* Worker/close retirement has separate production fixtures. */
        } else assert(!token.identity && !prepared && !started && closed==1 && released==1 && !s_wps_ap_radio);
    }
    return 0;
}
