
static void setup_recovery(void) {
    setup();recovery_admitted=true;recovery_owner=73;saved_phy_reads=0;change_home_after_phy=false;
    s_radio.operation.identity=91;s_radio.operation.generation=7;s_radio.leases[0].identity=73;
    native_band_mode=WIFI_BAND_MODE_2G_ONLY;
    native_channel=6;native_home=1; /* Temporary residency differs from home. */
}
static void check_no_mutation(void) {
    assert(!writes && !native_starts && !native_stops && !band_writes && !channel_writes && !nvs_writes);
    assert(s_radio.operation.identity==91 && s_radio.leases[0].identity==73 && s_radio.generation==7);
}
int main(void) {
    setup_recovery();wifi_radio_operation_lock();
    assert(wifi_radio_restart_configs_capture_locked(&token,WIFI_MODE_STA)==ESP_OK);
    check_no_mutation();assert(s_config_restart.captured && s_config_restart.snapshot);
    assert(s_config_restart.snapshot->primary==1 && s_config_restart.snapshot->band==WIFI_BAND_2G);
    assert(s_config_restart.snapshot->visible_bands==1 && s_config_restart.snapshot->tx_power==52);
    assert(!memcmp(s_config_restart.snapshot->saved[0].sta.password,"secret-sta",10));
#if CONFIG_SOC_WIFI_SUPPORT_5G
    assert(saved_phy_reads==(CONFIG_ESP_WIFI_SOFTAP_SUPPORT?2U:1U));
    assert(s_config_restart.snapshot->protocols[0].ghz_5g==native_protocols[0].ghz_5g);
    assert(s_config_restart.snapshot->bandwidths[0].ghz_5g==native_bandwidths[0].ghz_5g);
#endif
    unsigned full_calls=calls;wifi_radio_restart_configs_t frozen=*s_config_restart.snapshot;
    native_config[0].sta.password[0]='X';native_protocols[0].ghz_2g=0;
    assert(wifi_radio_restart_configs_capture_locked(&token,WIFI_MODE_STA)==ESP_OK && calls==full_calls);
    assert(!memcmp(&frozen,s_config_restart.snapshot,sizeof(frozen)));
    wipe();wifi_radio_operation_unlock();

    for(unsigned fail=1;fail<=full_calls;++fail) {
        setup_recovery();fail_at=fail;wifi_radio_operation_lock();
        assert(wifi_radio_restart_configs_capture_locked(&token,WIFI_MODE_STA)!=ESP_OK);
        check_no_mutation();assert(!allocation && !s_config_restart.captured && !s_config_restart.snapshot);
        assert(allocations==frees);wifi_radio_operation_unlock();
    }
    setup_recovery();allocation_fail=true;wifi_radio_operation_lock();
    assert(wifi_radio_restart_configs_capture_locked(&token,WIFI_MODE_STA)==ESP_ERR_NO_MEM);
    check_no_mutation();assert(!allocation && !calls);wifi_radio_operation_unlock();
    setup_recovery();s_radio.leases[1].identity=74;wifi_radio_operation_lock();
    assert(wifi_radio_restart_configs_capture_locked(&token,WIFI_MODE_STA)==ESP_ERR_INVALID_STATE && !allocation && !calls);
    wifi_radio_operation_unlock();
    setup_recovery();recovery_owner=0;wifi_radio_operation_lock();
    assert(wifi_radio_restart_configs_capture_locked(&token,WIFI_MODE_STA)==ESP_ERR_INVALID_STATE && !allocation && !calls);
    wifi_radio_operation_unlock();
#if CONFIG_SOC_WIFI_SUPPORT_5G
    setup_recovery();change_home_after_phy=true;wifi_radio_operation_lock();
    assert(wifi_radio_restart_configs_capture_locked(&token,WIFI_MODE_STA)==ESP_ERR_INVALID_RESPONSE);
    check_no_mutation();assert(!allocation && allocations==frees);wifi_radio_operation_unlock();
    setup_recovery();native_protocols[0].ghz_5g=0;wifi_radio_operation_lock();
    assert(wifi_radio_restart_configs_capture_locked(&token,WIFI_MODE_STA)==ESP_ERR_INVALID_RESPONSE);
    check_no_mutation();assert(!allocation && allocations==frees);wifi_radio_operation_unlock();
    setup_recovery();native_band_mode=WIFI_BAND_MODE_5G_ONLY;native_home=36;
    native_channel=1;native_band=WIFI_BAND_2G;wifi_radio_operation_lock();
    assert(wifi_radio_restart_configs_capture_locked(&token,WIFI_MODE_STA)==ESP_OK);
    check_no_mutation();assert(s_config_restart.snapshot->primary==36 && s_config_restart.snapshot->band==WIFI_BAND_5G);
    assert(s_config_restart.snapshot->protocols[0].ghz_2g==native_protocols[0].ghz_2g);
    wipe();wifi_radio_operation_unlock();
#endif
    assert(!allocation && !locks && !critical);return 0;
}
