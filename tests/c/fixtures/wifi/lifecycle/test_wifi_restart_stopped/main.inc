
static void stop_source(unsigned band) {
    setup();s_radio.event_identity=51;
#if CONFIG_SOC_WIFI_SUPPORT_5G
    native_band_mode=band==1 ? WIFI_BAND_MODE_2G_ONLY : band==2 ? WIFI_BAND_MODE_5G_ONLY : WIFI_BAND_MODE_AUTO;
    native_band=band==2 ? WIFI_BAND_5G : WIFI_BAND_2G;
    native_channel=native_home=band==2 ? 36 : 1;
#else
    (void)band;
#endif
    wifi_radio_operation_lock();
    wifi_radio_capture_stop_snapshot_locked();
    assert(wifi_radio_stop_locked()==ESP_OK && wifi_radio_stop_snapshot_unchanged_locked());
    /* SDK output unavailable after STOP must not replace historical values. */
    native_tx_power=99;native_channel=native_home=0;
    calls=writes=fail_at=0;
}
int main(void) {
    stop_source(0);
    assert(wifi_radio_restart_configs_capture_locked(&token,WIFI_MODE_STA)==ESP_OK);
    unsigned read_calls=calls;
    assert(!writes && !native_running && s_config_restart.snapshot->tx_power==52 && s_config_restart.snapshot->primary==1);
    wipe();wifi_radio_operation_unlock();
    for(unsigned failure=1;failure<=read_calls;++failure) {
        stop_source(0);fail_at=failure;
        assert(wifi_radio_restart_configs_capture_locked(&token,WIFI_MODE_STA)==77);
        assert(!allocation && !writes && !native_running && frees==1 && token.identity==41);
        wifi_radio_operation_unlock();
    }
    stop_source(0);allocation_fail=true;
    assert(wifi_radio_restart_configs_capture_locked(&token,WIFI_MODE_STA)==ESP_ERR_NO_MEM && !calls && !allocation && !native_running);
    wifi_radio_operation_unlock();
    stop_source(0);
    assert(esp_wifi_set_mode(WIFI_MODE_STA)==ESP_OK);calls=0;
    native_tx_power=48;native_channel=native_home=1;
    assert(wifi_radio_restart_configs_capture_locked(&token,WIFI_MODE_STA)==ESP_OK && native_starts==1);
    assert(s_config_restart.snapshot->tx_power==48); /* Read this START, not invalidated STOP history. */
    assert(wifi_radio_stop_locked()==ESP_OK);wipe();
    wifi_radio_operation_unlock();
    stop_source(0);++s_radio.event_identity;
    native_tx_power=44;native_channel=native_home=1;
    assert(wifi_radio_restart_configs_capture_locked(&token,WIFI_MODE_STA)==ESP_OK && native_starts==1);
    assert(s_config_restart.snapshot->tx_power==44);
    assert(wifi_radio_stop_locked()==ESP_OK);wipe();
    wifi_radio_operation_unlock();
    stop_source(0);native_band_mode=(wifi_band_mode_t)257;
    assert(wifi_radio_restart_configs_capture_locked(&token,WIFI_MODE_STA)==ESP_ERR_INVALID_RESPONSE && !allocation && !writes);
    wifi_radio_operation_unlock();
#if CONFIG_SOC_WIFI_SUPPORT_5G
    for(unsigned band=1;band<=2;++band) {
        stop_source(band);
        assert(wifi_radio_restart_configs_capture_locked(&token,WIFI_MODE_STA)==ESP_OK);
        wifi_radio_restart_configs_t *frozen=s_config_restart.snapshot;
        assert(frozen && frozen->tx_power==52 && frozen->primary==(band==2 ? 36 : 1));
        assert(frozen->band_mode==(band==2 ? WIFI_BAND_MODE_5G_ONLY : WIFI_BAND_MODE_2G_ONLY));
        assert(native_band_mode==WIFI_BAND_MODE_AUTO && !s_stop_snapshot.unchanged && s_config_restart.captured);
        assert(!memcmp(frozen->saved[0].sta.password,"secret-sta",10));
        assert(native_storage==WIFI_STORAGE_RAM && nvs_writes==0);
        new_driver();
        assert(wifi_radio_restart_configs_replay_locked(&token)==ESP_OK);
        assert(s_config_restart.snapshot==frozen && frozen->tx_power==52 && frozen->primary==(band==2 ? 36 : 1));
        assert(native_band_mode==frozen->band_mode && !s_radio.started && !s_radio.stop_required);
        wipe();wifi_radio_operation_unlock();
    }
#endif
    assert(!allocation && !locks && !critical && !helper_locks);
    return 0;
}
