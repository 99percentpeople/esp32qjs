
int main(void) {
    setup();wifi_radio_operation_lock();new_driver();
    native_mode=s_radio.effective_mode=WIFI_MODE_APSTA;
    native_config[1].ap.authmode=WIFI_AUTH_OPEN;
    native_config[1].ap.pmf_cfg.capable=true;
    memcpy(native_config[1].ap.ssid,"ESP_default",11);
    native_config[1].ap.ssid_len=11;
    wifi_radio_restart_configs_t snapshot={0};
    snapshot.saved[1]=native_config[1];
    wifi_config_t frozen=snapshot.saved[1];
    unsigned before=writes;
    assert(wifi_radio_restart_config_io(&snapshot,1,true)==ESP_OK);
    assert(wifi_radio_restart_config_io(&snapshot,1,false)==ESP_OK);
    assert(writes==before && native_config[1].ap.pmf_cfg.capable);
    assert(!memcmp(&frozen,&snapshot.saved[1],sizeof(frozen)));
    wifi_config_t zero={0};
    assert(!memcmp(&zero,&snapshot.scratch,sizeof(zero)));
    /* A failed initial read must not proceed with a speculative write. */
    fail_at=calls+1;
    assert(wifi_radio_restart_config_io(&snapshot,1,true)==77);
    assert(writes==before && !memcmp(&zero,&snapshot.scratch,sizeof(zero)));
    fail_at=0;
    /* A different secured config must still be applied and checked exactly. */
    snapshot.saved[1].ap.authmode=WIFI_AUTH_WPA2_PSK;
    snapshot.saved[1].ap.pmf_cfg.required=true;
    memcpy(snapshot.saved[1].ap.password,"test-secret",11);
    assert(wifi_radio_restart_config_io(&snapshot,1,true)==ESP_OK);
    assert(writes>before);
    assert(wifi_radio_restart_config_io(&snapshot,1,false)==ESP_OK);
    native_config[1].ap.pmf_cfg.capable=false;
    assert(wifi_radio_restart_config_io(&snapshot,1,false)==ESP_ERR_INVALID_RESPONSE);
    native_config[1]=snapshot.saved[1];native_config[1].ap.password[0]^=1;
    assert(wifi_radio_restart_config_io(&snapshot,1,false)==ESP_ERR_INVALID_RESPONSE);
    assert(!nvs_writes && !memcmp(&zero,&snapshot.scratch,sizeof(zero)));
    wifi_radio_operation_unlock();return 0;
}
