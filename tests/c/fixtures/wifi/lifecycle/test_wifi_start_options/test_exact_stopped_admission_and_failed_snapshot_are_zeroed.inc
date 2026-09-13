
int main(void) {
    for(int scenario=0;scenario<6;scenario++) {
        s_radio.lifecycle=(esp32_mquickjs_wifi_radio_lifecycle_t){1,7};s_radio.driver_state=2;
        s_radio.driver_owned=s_radio.storage_configured=true;s_radio.leases[0].identity=0;
        getter_error=scenario==1;validation_error=scenario==2;regulatory_error=scenario==3;gets=validations=regulations=0;
        esp32_mquickjs_wifi_radio_lifecycle_t token={1,7};
        if(scenario==4)token.generation=2;
        if(scenario==5)s_radio.leases[0].identity=9;
        wifi_config_t config;memset(&config,0x33,sizeof(config));
        int err=esp32_mquickjs_wifi_radio_copy_stopped_ap_configuration(&token,&config);
        if(!scenario)assert(err==0 && config.ap.channel==6 && config.secret[0]==0x55 && gets==1 && regulations==1);
        else {
            assert(err!=0);for(size_t i=0;i<sizeof(config);i++)assert(!((unsigned char *)&config)[i]);
            if(scenario>=4)assert(!gets && !validations && !regulations);
        }
        assert(s_radio.lifecycle.identity==7 && !depth);
        esp32_mquickjs_wireless_secure_zero(&config,sizeof(config));
    }
}
