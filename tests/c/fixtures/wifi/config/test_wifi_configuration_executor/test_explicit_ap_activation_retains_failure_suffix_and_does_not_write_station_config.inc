
int main(void) {
    wifi_config_t ap={27};expected_allow_disconnect=true;
    assert(esp32_mquickjs_wifi_activate_ap(NULL)==ESP_ERR_INVALID_ARG && !calls[1]);
    for(activation_mode=WIFI_MODE_AP;activation_mode<=WIFI_MODE_APSTA;activation_mode++) {
        for(fail_at=0;fail_at<=11;fail_at++) {
            if(activation_mode==WIFI_MODE_AP && fail_at==7)continue;
            reset_fixture();expect_ap_only=true;
            int err=esp32_mquickjs_wifi_activate_ap(&ap);
            assert(ap.value==27);
            if(!fail_at) {
                assert(!err && calls[3]==1 && calls[10]==1 && calls[11]==1);
                assert(prepared_sta==(activation_mode==WIFI_MODE_APSTA) && prepared_ap);
                assert(!s_wifi_configuration_cleanup && !s_wifi_lifecycle.identity);
            } else {
                assert(err!=ESP_OK);
                if(fail_at<=2)assert(!s_wifi_configuration_cleanup && !calls[3]);
                else {
                    assert(s_wifi_configuration_cleanup && s_wifi_lifecycle.identity);
                    int before[12];memcpy(before,calls,sizeof(calls));
                    assert(esp32_mquickjs_wifi_activate_ap(&ap)==ESP_ERR_INVALID_STATE);
                    assert(!memcmp(before,calls,sizeof(calls)));
                    assert(wifi_finish_configuration_cleanup()==ESP_OK);
                }
            }
        }
    }
}
