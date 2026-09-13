
static esp32_mquickjs_wifi_radio_lifecycle_t token={.generation=7,.identity=41};
static void setup(void) {
    assert(!allocation);reset();memset(&s_config_restart,0,sizeof(s_config_restart));
#if CONFIG_SOC_WIFI_HE_SUPPORT && CONFIG_IDF_TARGET_ESP32C5
    memset(&s_twt_policy,0,sizeof(s_twt_policy));native_twt_policy=(wifi_twt_config_t){0};
#endif
    memset(&s_stop_snapshot,0,sizeof(s_stop_snapshot));
    memset(&s_scan_parameters,0,sizeof(s_scan_parameters));
    native_scan=(wifi_scan_default_params_t)WIFI_SCAN_PARAMS_DEFAULT_CONFIG();
    scan_partial_failure=scan_corrupt=false;scan_writes=scan_resets=0;
    native_running=true;band_cycle_pending=false;native_starts=native_stops=0;
    memset(inactive_reads,0,sizeof(inactive_reads));memset(inactive_writes,0,sizeof(inactive_writes));
    inactive_nvs_writes=0;corrupt_inactive=false;inactive[0]=17;inactive[1]=601;
    s_radio.lifecycle.identity=41;s_radio.lifecycle.generation=7;
    s_radio.started=true;s_radio.stop_required=true;s_radio.driver_state=ESP32_MQUICKJS_WIFI_RADIO_STARTED;
    s_radio.effective_mode=native_mode=WIFI_MODE_STA;s_radio.storage=native_storage=WIFI_STORAGE_FLASH;
    memset(s_radio.leases,0,sizeof(s_radio.leases));memset(native_config,0,sizeof(native_config));
    memcpy(native_config[0].sta.ssid,"private-sta",11);memcpy(native_config[0].sta.password,"secret-sta",10);
    native_config[0].sta.pmf_cfg.required=true;
    memcpy(native_config[1].ap.ssid,"private-ap",10);memcpy(native_config[1].ap.password,"secret-ap",9);
    native_config[1].ap.ssid_len=10;native_config[1].ap.pmf_cfg.required=true;
    native_country=(wifi_country_t){.cc="US",.schan=1,.nchan=11,.max_tx_power=19,.policy=WIFI_COUNTRY_POLICY_MANUAL};
#if CONFIG_SOC_WIFI_SUPPORT_5G
    native_country.wifi_5g_channel_mask=1;
#endif
    native_ps=WIFI_PS_MAX_MODEM;native_event_mask=0;
    memcpy(native_mac[0],factory_mac[CONFIG_ESP_WIFI_SOFTAP_SUPPORT ? 1 : 0],6);
    memcpy(native_mac[1],factory_mac[0],6); /* Saved pair swapped relative to new-driver defaults. */
    native_tx_power=52;clamp_tx_power=false;
    native_band=WIFI_BAND_2G;native_channel=native_home=1;
    native_secondary=native_home_secondary=WIFI_SECOND_CHAN_NONE;
    band_writes=channel_writes=0;memset(phy_reads,0,sizeof(phy_reads));change_visible_phy=ignore_channel=false;
    native_band_mode=CONFIG_SOC_WIFI_SUPPORT_5G ? WIFI_BAND_MODE_AUTO : WIFI_BAND_MODE_2G_ONLY;
    for(unsigned i=0;i<2;++i) {
        native_protocols[i]=(wifi_protocols_t){.ghz_2g=WIFI_PROTOCOL_11B|WIFI_PROTOCOL_11G|WIFI_PROTOCOL_11N,.ghz_5g=WIFI_PROTOCOL_11A|WIFI_PROTOCOL_11N};
        native_bandwidths[i]=(wifi_bandwidths_t){.ghz_2g=i ? WIFI_BW20 : WIFI_BW40,.ghz_5g=WIFI_BW20};
    }
    memset(&s_tx_rate_lease,0,sizeof(s_tx_rate_lease));memset(&s_tx_rates,0,sizeof(s_tx_rates));
    memset(native_rates,0,sizeof(native_rates));memset(rate_writes,0,sizeof(rate_writes));
    for(unsigned i=0;i<(CONFIG_ESP_WIFI_SOFTAP_SUPPORT ? 2U : 1U);++i) {
        native_rates[i]=(wifi_tx_rate_config_t){.phymode=WIFI_PHY_MODE_11G,.rate=i ? WIFI_PHY_RATE_12M : WIFI_PHY_RATE_6M};
        s_tx_rates.records[i]=(esp32_mquickjs_wifi_tx_rate_record_t){.known=true,.generation=7,.write_identity=i+1,.config=native_rates[i]};
    }
    s_tx_rates.next_identity=CONFIG_ESP_WIFI_SOFTAP_SUPPORT ? 3 : 2;
    s_interval=(esp32_mquickjs_wifi_interval_state_t){.generation=7,.revision=5,.value=300,.known=true};native_interval=300;
    allocations=frees=nvs_writes=mac_writes=power_writes=0;allocation_fail=mutate_input=corrupt_config=reenable_pmf=false;cleanup_error=0;
    token=(esp32_mquickjs_wifi_radio_lifecycle_t){.generation=7,.identity=41};
}
static void new_driver(void) {
#if CONFIG_SOC_WIFI_HE_SUPPORT && CONFIG_IDF_TARGET_ESP32C5
    memset(&s_twt_policy,0,sizeof(s_twt_policy));native_twt_policy=(wifi_twt_config_t){0};
#endif
    memset(&s_scan_parameters,0,sizeof(s_scan_parameters));
    native_scan=(wifi_scan_default_params_t)WIFI_SCAN_PARAMS_DEFAULT_CONFIG();
    scan_partial_failure=scan_corrupt=false;scan_writes=scan_resets=0;
    memset(&s_inactive_history,0,sizeof(s_inactive_history)); /* Proven physical deinit boundary. */
    esp32_mquickjs_wifi_tx_rate_invalidate(&s_tx_rates);
    memset(native_rates,0,sizeof(native_rates));memset(rate_writes,0,sizeof(rate_writes));
    assert(esp32_mquickjs_wifi_interval_invalidate(&s_interval,s_radio.generation));
    ++s_radio.generation;memset(native_config,0,sizeof(native_config));
    native_running=band_cycle_pending=false;native_starts=native_stops=0;
    inactive[0]=6;inactive[1]=300;
    memset(inactive_reads,0,sizeof(inactive_reads));memset(inactive_writes,0,sizeof(inactive_writes));
    inactive_nvs_writes=0;corrupt_inactive=false;
    native_band=WIFI_BAND_2G;native_channel=native_home=1;
    native_secondary=native_home_secondary=WIFI_SECOND_CHAN_NONE;channel_writes=band_writes=0;
    native_band_mode=CONFIG_SOC_WIFI_SUPPORT_5G ? WIFI_BAND_MODE_AUTO : WIFI_BAND_MODE_2G_ONLY;
    for(unsigned i=0;i<2;++i) {
        native_protocols[i]=(wifi_protocols_t){.ghz_2g=WIFI_PROTOCOL_11B|WIFI_PROTOCOL_11G,.ghz_5g=WIFI_PROTOCOL_11A};
        native_bandwidths[i]=(wifi_bandwidths_t){.ghz_2g=WIFI_BW20,.ghz_5g=WIFI_BW20};
    }
    calls=writes=fail_at=0;
    esp32_mquickjs_wifi_interval_result_t baseline;
    assert(esp32_mquickjs_wifi_interval_write(&s_interval,s_radio.generation,0,wifi_radio_interval_writer,NULL,&baseline)==ESP_OK);
    native_country=(wifi_country_t){.cc="01",.schan=1,.nchan=11,.max_tx_power=20,.policy=WIFI_COUNTRY_POLICY_AUTO};
    native_ps=WIFI_PS_MIN_MODEM;native_event_mask=WIFI_EVENT_MASK_AP_PROBEREQRECVED;
    memcpy(native_mac,factory_mac,sizeof(native_mac));mac_writes=power_writes=0;native_tx_power=80;
    s_radio.storage=native_storage=WIFI_STORAGE_RAM;s_radio.effective_mode=native_mode=WIFI_MODE_STA;
    s_radio.started=s_radio.stop_required=false;s_radio.driver_state=ESP32_MQUICKJS_WIFI_RADIO_STOPPED;
    s_radio.fault_stage=NULL;s_radio.cleanup_stage=NULL;s_radio.fault_error=0;
    s_radio.driver_owned=s_radio.storage_configured=true;calls=writes=fail_at=0;
}
static void wipe(void) {wifi_radio_restart_configs_discard_locked(&token);assert(!allocation && !s_config_restart.captured);}
static void disabled_pmf_replay(void) {
    setup();wifi_radio_operation_lock();
    native_config[0].sta.threshold.authmode=WIFI_AUTH_WPA2_PSK;
    native_config[0].sta.disable_wpa3_compatible_mode=true;
    native_config[0].sta.pmf_cfg=(wifi_pmf_config_t){0};
    native_config[1].ap.authmode=WIFI_AUTH_WPA2_PSK;
    native_config[1].ap.pmf_cfg=(wifi_pmf_config_t){0};
    assert(wifi_radio_restart_configs_capture_locked(&token,WIFI_MODE_STA)==ESP_OK);
    wifi_radio_restart_configs_t frozen=*s_config_restart.snapshot;
    new_driver();reenable_pmf=true;
    assert(wifi_radio_restart_configs_replay_locked(&token)==ESP_OK);
    assert(!native_config[0].sta.pmf_cfg.capable && !native_config[0].sta.pmf_cfg.required);
    if(CONFIG_ESP_WIFI_SOFTAP_SUPPORT)assert(!native_config[1].ap.pmf_cfg.capable && !native_config[1].ap.pmf_cfg.required);
    assert(!nvs_writes && !memcmp(&frozen,s_config_restart.snapshot,sizeof(frozen)));
    assert(wifi_radio_restart_configs_pre_start_locked(&token,WIFI_MODE_STA)==ESP_OK);
    wipe();wifi_radio_operation_unlock();
}
static void pre_start_cases(void) {
    const wifi_mode_t modes[]={WIFI_MODE_STA,WIFI_MODE_AP,WIFI_MODE_APSTA};
    for(unsigned mi=0;mi<(CONFIG_ESP_WIFI_SOFTAP_SUPPORT ? 3U : 1U);++mi) {
        for(unsigned scenario=0;scenario<7;++scenario) {
            setup();wifi_radio_operation_lock();native_config[1].ap.channel=1;
            s_radio.effective_mode=native_mode=modes[mi];
            if(mi && scenario==5)native_config[1].ap.channel=6; /* Legitimate former STA-following mismatch. */
            if(mi && scenario==6)native_config[1].ap.channel=0; /* SDK selects startup; CSA restores the frozen actual channel. */
            assert(wifi_radio_restart_configs_capture_locked(&token,modes[mi])==ESP_OK);
            new_driver();assert(wifi_radio_restart_configs_replay_locked(&token)==ESP_OK);
            wifi_radio_restart_configs_t frozen=*s_config_restart.snapshot;
            unsigned before=calls,write_count=writes,start_count=native_starts;
            if(scenario==1)native_config[0].sta.pmf_cfg.required=false;
            if(scenario==2 && CONFIG_ESP_WIFI_SOFTAP_SUPPORT)native_config[1].ap.password[0]^=1;
            if(scenario==3)fail_at=calls+1;
            if(scenario==4)fail_at=calls+(CONFIG_ESP_WIFI_SOFTAP_SUPPORT ? 2 : 1);
            esp_err_t expected=(scenario==3 || scenario==4) ? 77 :
                (scenario==1 || (scenario==2 && CONFIG_ESP_WIFI_SOFTAP_SUPPORT)) ? ESP_ERR_INVALID_RESPONSE :
                ESP_OK;
            assert(wifi_radio_restart_configs_pre_start_locked(&token,modes[mi])==expected);
            assert(calls>before && writes==write_count && native_starts==start_count && !s_radio.started);
            assert(!memcmp(&frozen,s_config_restart.snapshot,sizeof(frozen)) && allocation && !frees);
            if(expected!=ESP_OK) {
                assert(s_radio.fault_stage && s_radio.configuration.error==expected);
                before=calls;assert(wifi_radio_restart_configs_pre_start_locked(&token,modes[mi])==ESP_ERR_INVALID_STATE && calls==before);
            }
            wipe();wifi_radio_operation_unlock();
        }
    }
    for(unsigned bad=0;bad<8;++bad) {
        setup();wifi_radio_operation_lock();
        assert(wifi_radio_restart_configs_capture_locked(&token,WIFI_MODE_STA)==ESP_OK);
        new_driver();assert(wifi_radio_restart_configs_replay_locked(&token)==ESP_OK);
        esp32_mquickjs_wifi_radio_lifecycle_t request=token;
        switch(bad) {
            case 0:request.identity++;break;
            case 1:request.generation++;break;
            case 2:s_config_restart.replay_generation--;break;
            case 3:s_radio.leases[0].identity=99;break;
            case 4:s_radio.operation.identity=1;break;
            case 5:s_radio.wake_locks=1;break;
            case 6:s_radio.started=true;break;
            case 7:s_radio.cleanup_stage="pending";break;
        }
        unsigned before=calls;
        assert(wifi_radio_restart_configs_pre_start_locked(&request,WIFI_MODE_STA)==ESP_ERR_INVALID_STATE && calls==before);
        wipe();wifi_radio_operation_unlock();
    }
}
static void restore_cases(void) {
    for(unsigned failure=1;failure<=6;++failure) {
        setup();wifi_radio_operation_lock();native_channel=native_home=6;
        assert(wifi_radio_restart_configs_capture_locked(&token,WIFI_MODE_STA)==ESP_OK);
        wifi_radio_restart_configs_t frozen=*s_config_restart.snapshot;
        new_driver();assert(wifi_radio_restart_configs_replay_locked(&token)==ESP_OK);
        s_radio.started=true;s_radio.driver_state=ESP32_MQUICKJS_WIFI_RADIO_STARTED;
        if(failure<=5)fail_at=calls+failure;else ignore_channel=true;
        assert(wifi_radio_restart_configs_post_start_locked(&token)==(failure<=5 ? 77 : ESP_ERR_INVALID_RESPONSE));
        assert(channel_writes==1 && !power_writes && allocation && !memcmp(&frozen,s_config_restart.snapshot,sizeof(frozen)));
        assert(s_config_restart.start_phase==(failure==1 ? RESTART_START_CHANNEL : RESTART_START_CHANNEL_READ));
        unsigned before=calls;
        assert(wifi_radio_restart_configs_post_start_locked(&token)==ESP_ERR_INVALID_STATE && calls==before);
        new_driver();ignore_channel=false;
        assert(wifi_radio_restart_configs_replay_locked(&token)==ESP_OK);
        s_radio.started=true;s_radio.driver_state=ESP32_MQUICKJS_WIFI_RADIO_STARTED;
        assert(wifi_radio_restart_configs_post_start_locked(&token)==ESP_OK && channel_writes==1 && native_channel==6 && native_home==6);
        assert(wifi_radio_restart_configs_verify_locked(&token,WIFI_MODE_STA)==ESP_OK);
        wipe();wifi_radio_operation_unlock();
    }
#if CONFIG_ESP_WIFI_SOFTAP_SUPPORT
    for(unsigned mode=WIFI_MODE_AP;mode<=WIFI_MODE_APSTA;++mode) {
        setup();wifi_radio_operation_lock();native_channel=native_home=6;
        s_radio.effective_mode=native_mode=(wifi_mode_t)mode;
        assert(wifi_radio_restart_configs_capture_locked(&token,(wifi_mode_t)mode)==ESP_OK);
        new_driver();assert(wifi_radio_restart_configs_replay_locked(&token)==ESP_OK);
        s_radio.started=true;s_radio.driver_state=ESP32_MQUICKJS_WIFI_RADIO_STARTED;
        assert(wifi_radio_restart_configs_post_start_locked(&token)==ESP_ERR_INVALID_RESPONSE);
        assert(!channel_writes && !power_writes && !strcmp(s_radio.configuration.stage,"restart-channel-readback"));
        assert(allocation);wipe();wifi_radio_operation_unlock();
    }
#endif
#if CONFIG_SOC_WIFI_SUPPORT_5G
    /* AUTO also needs its original current band, rather than an assumed 2G default. */
    setup();wifi_radio_operation_lock();native_band=WIFI_BAND_5G;native_channel=native_home=44;
    assert(wifi_radio_restart_configs_capture_locked(&token,WIFI_MODE_STA)==ESP_OK);
    new_driver();assert(wifi_radio_restart_configs_replay_locked(&token)==ESP_OK);
    assert(!native_starts && !native_stops && !band_writes);
    s_radio.started=true;s_radio.driver_state=ESP32_MQUICKJS_WIFI_RADIO_STARTED;
    assert(wifi_radio_restart_configs_post_start_locked(&token)==ESP_OK);
    assert(native_band_mode==WIFI_BAND_MODE_AUTO && native_band==WIFI_BAND_5G && native_channel==44);
    assert(wifi_radio_restart_configs_verify_locked(&token,WIFI_MODE_STA)==ESP_OK);
    wipe();wifi_radio_operation_unlock();
    setup();wifi_radio_operation_lock();native_band_mode=WIFI_BAND_MODE_2G_ONLY;
    assert(wifi_radio_restart_configs_capture_locked(&token,WIFI_MODE_STA)==ESP_OK);
    new_driver();assert(wifi_radio_restart_configs_replay_locked(&token)==ESP_OK);
    unsigned replay_calls=calls;assert(native_starts==1 && native_stops==1 && native_band_mode==WIFI_BAND_MODE_2G_ONLY);
    wipe();wifi_radio_operation_unlock();
    for(unsigned failure=1;failure<=replay_calls;++failure) {
        setup();wifi_radio_operation_lock();native_band_mode=WIFI_BAND_MODE_2G_ONLY;
        assert(wifi_radio_restart_configs_capture_locked(&token,WIFI_MODE_STA)==ESP_OK);
        wifi_radio_restart_configs_t frozen=*s_config_restart.snapshot;
        new_driver();fail_at=failure;
        assert(wifi_radio_restart_configs_replay_locked(&token)==77 && allocation && !nvs_writes);
        assert(!memcmp(&frozen,s_config_restart.snapshot,sizeof(frozen)));
        for(unsigned i=0;i<WIFI_RADIO_MAX_LEASES;++i)assert(!s_radio.leases[i].identity);
        new_driver();assert(wifi_radio_restart_configs_replay_locked(&token)==ESP_OK);
        assert(native_starts==1 && native_stops==1 && !s_radio.started && !s_radio.stop_required);
        assert(native_band_mode==WIFI_BAND_MODE_2G_ONLY && !nvs_writes);
        s_radio.started=true;s_radio.driver_state=ESP32_MQUICKJS_WIFI_RADIO_STARTED;
        assert(wifi_radio_restart_configs_post_start_locked(&token)==ESP_OK);
        assert(wifi_radio_restart_configs_verify_locked(&token,WIFI_MODE_STA)==ESP_OK);
        wipe();wifi_radio_operation_unlock();
    }
#endif
}
#if CONFIG_SOC_WIFI_SUPPORT_5G
static void capture_cases(void) {
    /* Calls production capture/continuation. SDK fixtures hide inactive PHY;
     * the injected preparation resets the current channel, as the SDK may. */
#if CONFIG_ESP_WIFI_SOFTAP_SUPPORT
    for(unsigned mode=WIFI_MODE_AP;mode<=WIFI_MODE_APSTA;++mode) {
        setup();wifi_radio_operation_lock();native_band_mode=WIFI_BAND_MODE_2G_ONLY;
        s_radio.effective_mode=native_mode=(wifi_mode_t)mode;
        assert(wifi_radio_restart_configs_capture_locked(&token,(wifi_mode_t)mode)==ESP_OK);
        assert(s_config_restart.mode==(wifi_mode_t)mode && native_mode==WIFI_MODE_STA);
        assert(native_starts==1 && native_stops==1 && !nvs_writes);
        wipe();wifi_radio_operation_unlock();
    }
#endif
    for(unsigned band=0;band<2;++band) {
        setup();wifi_radio_operation_lock();
        native_band_mode=band ? WIFI_BAND_MODE_5G_ONLY : WIFI_BAND_MODE_2G_ONLY;
        native_band=band ? WIFI_BAND_5G : WIFI_BAND_2G;native_channel=native_home=band ? 36 : 6;
        assert(wifi_radio_restart_configs_capture_locked(&token,WIFI_MODE_STA)==ESP_OK);
        assert(s_config_restart.captured && allocations==1 && !frees && !nvs_writes);
        assert(native_starts==1 && native_stops==1 && band_writes==1 && native_running);
        assert(s_config_restart.snapshot->band_mode==(band ? WIFI_BAND_MODE_5G_ONLY : WIFI_BAND_MODE_2G_ONLY));
        assert(s_config_restart.snapshot->primary==(band ? 36 : 6) && native_channel==1);
        assert(s_config_restart.snapshot->storage==WIFI_STORAGE_FLASH && native_storage==WIFI_STORAGE_RAM);
        assert(s_config_restart.snapshot->protocols[0].ghz_2g && s_config_restart.snapshot->protocols[0].ghz_5g);
        for(unsigned i=0;i<WIFI_RADIO_MAX_LEASES;++i)assert(!s_radio.leases[i].identity);
        new_driver();assert(wifi_radio_restart_configs_replay_locked(&token)==ESP_OK);
        s_radio.started=true;s_radio.driver_state=ESP32_MQUICKJS_WIFI_RADIO_STARTED;
        assert(wifi_radio_restart_configs_post_start_locked(&token)==ESP_OK);
        assert(native_band_mode==s_config_restart.snapshot->band_mode && native_channel==s_config_restart.snapshot->primary);
        assert(channel_writes==1 && native_starts==1 && native_stops==1);
        assert(wifi_radio_restart_configs_verify_locked(&token,WIFI_MODE_STA)==ESP_OK);
        native_home=native_channel==1 ? 6 : 1;
        assert(wifi_radio_restart_configs_verify_locked(&token,WIFI_MODE_STA)==ESP_ERR_INVALID_STATE);
        assert(!strcmp(s_radio.configuration.stage,"restart-band-channel-final-readback") && allocation);
        wipe();wifi_radio_operation_unlock();
    }
    setup();wifi_radio_operation_lock();native_band_mode=WIFI_BAND_MODE_2G_ONLY;
    assert(wifi_radio_restart_configs_capture_locked(&token,WIFI_MODE_STA)==ESP_OK);
    unsigned capture_calls=calls;wipe();wifi_radio_operation_unlock();
    for(unsigned failure=1;failure<=capture_calls;++failure) {
        setup();wifi_radio_operation_lock();native_band_mode=WIFI_BAND_MODE_2G_ONLY;fail_at=failure;
        assert(wifi_radio_restart_configs_capture_locked(&token,WIFI_MODE_STA)==77);
        if(!allocation) {assert(!writes && frees==1);wifi_radio_operation_unlock();continue;}
        assert(!s_config_restart.captured && allocations==1 && !frees && !nvs_writes);
        assert(s_config_restart.snapshot->storage==WIFI_STORAGE_FLASH);
        assert(s_config_restart.snapshot->band_mode==WIFI_BAND_MODE_2G_ONLY);
        assert(!memcmp(s_config_restart.snapshot->saved[0].sta.password,"secret-sta",10));
        assert(!wifi_radio_restart_configs_ready_locked(&token,WIFI_MODE_STA));
        assert(wifi_radio_restart_configs_post_start_locked(&token)==ESP_ERR_INVALID_STATE);
        unsigned before=calls,starts=native_starts,band_changes=band_writes,sta_reads=phy_reads[0];
        uint8_t phase=s_config_restart.snapshot->capture_phase;
        esp32_mquickjs_wifi_radio_lifecycle_t stale=token;stale.identity++;
        assert(wifi_radio_restart_configs_capture_locked(&stale,WIFI_MODE_STA)==ESP_ERR_INVALID_STATE && calls==before);
        assert(wifi_radio_restart_configs_capture_locked(&token,WIFI_MODE_AP)==ESP_ERR_INVALID_STATE && calls==before);
        ++s_radio.generation;
        assert(wifi_radio_restart_configs_capture_locked(&token,WIFI_MODE_STA)==ESP_ERR_INVALID_STATE && calls==before);
        --s_radio.generation;fail_at=0;
        if(phase==RESTART_CAPTURE_FAILED) {
            assert(wifi_radio_restart_configs_capture_locked(&token,WIFI_MODE_STA)==77 && calls==before);
        } else {
            assert(wifi_radio_restart_configs_capture_locked(&token,WIFI_MODE_STA)==ESP_OK && allocations==1);
            if(phase>=RESTART_CAPTURE_START_EVENTS)assert(native_starts==starts);
            if(phase>=RESTART_CAPTURE_AUTO_READ)assert(band_writes==band_changes);
            if(phase==RESTART_CAPTURE_AP_PHY)assert(phy_reads[0]==sta_reads);
        }
        /* A failed explicit cleanup retains the same allocation and owner. */
        wifi_radio_operation_unlock();cleanup_error=77;
        assert(esp32_mquickjs_wifi_radio_finish_lifecycle(&token,true)==77 && allocation && token.identity==41);
        cleanup_error=0;assert(esp32_mquickjs_wifi_radio_finish_lifecycle(&token,true)==ESP_OK && !allocation && !token.identity);
    }
    setup();wifi_radio_operation_lock();native_band_mode=WIFI_BAND_MODE_2G_ONLY;change_visible_phy=true;
    assert(wifi_radio_restart_configs_capture_locked(&token,WIFI_MODE_STA)==ESP_ERR_INVALID_RESPONSE);
    assert(allocation && !s_config_restart.captured && s_config_restart.snapshot->capture_phase==RESTART_CAPTURE_STA_PHY);
    assert(s_config_restart.snapshot->protocols[0].ghz_2g==(WIFI_PROTOCOL_11B|WIFI_PROTOCOL_11G|WIFI_PROTOCOL_11N));
    assert(s_config_restart.snapshot->protocols[0].ghz_5g==0); /* No partial assignment on comparison failure. */
    wipe();wifi_radio_operation_unlock();
    setup();wifi_radio_operation_lock();native_home=6;
    assert(wifi_radio_restart_configs_capture_locked(&token,WIFI_MODE_STA)==ESP_ERR_INVALID_STATE);
    assert(!allocation && !writes && frees==1);wifi_radio_operation_unlock();
}
#endif
int main(void) {
    disabled_pmf_replay();
    pre_start_cases();
    restore_cases();
#if CONFIG_SOC_WIFI_SUPPORT_5G
    capture_cases();
#endif
    setup();wifi_radio_operation_lock();allocation_fail=true;
    assert(wifi_radio_restart_configs_capture_locked(&token,WIFI_MODE_STA)==ESP_ERR_NO_MEM && !calls && !allocation && !s_config_restart.captured);
    wifi_radio_operation_unlock();
    setup();wifi_radio_operation_lock();
    assert(wifi_radio_restart_configs_capture_locked(&token,WIFI_MODE_STA)==ESP_OK);
    unsigned capture_calls=calls;wipe();wifi_radio_operation_unlock();
    for(unsigned failure=1;failure<=capture_calls;++failure) {
        setup();wifi_radio_operation_lock();fail_at=failure;
        assert(wifi_radio_restart_configs_capture_locked(&token,WIFI_MODE_STA)==77 && frees==1 && !allocation && !s_config_restart.captured && !writes);
        wifi_radio_operation_unlock();
    }
    /* Discover the SDK boundary count from a successful production replay. */
    setup();wifi_radio_operation_lock();
    assert(wifi_radio_restart_configs_capture_locked(&token,WIFI_MODE_STA)==ESP_OK);
    new_driver();assert(wifi_radio_restart_configs_replay_locked(&token)==ESP_OK);
    unsigned replay_calls=calls;assert(replay_calls>0);wipe();wifi_radio_operation_unlock();
    for(unsigned failure=1;failure<=replay_calls;++failure) {
        setup();wifi_radio_operation_lock();
        assert(wifi_radio_restart_configs_capture_locked(&token,WIFI_MODE_STA)==ESP_OK && !writes);
        assert(s_config_restart.snapshot->mask==(CONFIG_ESP_WIFI_SOFTAP_SUPPORT ? 3 : 1));
        wifi_radio_restart_configs_t frozen=*s_config_restart.snapshot;
        unsigned before=calls;
        assert(wifi_radio_restart_configs_replay_locked(&token)==ESP_ERR_INVALID_STATE && calls==before); /* No physical transition. */
        new_driver();fail_at=failure;mutate_input=true;
        assert(wifi_radio_restart_configs_replay_locked(&token)==77 && allocation && !nvs_writes);
        assert(!memcmp(&frozen,s_config_restart.snapshot,sizeof(frozen)) && !wifi_radio_restart_configs_ready_locked(&token,WIFI_MODE_STA));
        before=calls;assert(wifi_radio_restart_configs_capture_locked(&token,WIFI_MODE_STA)==ESP_OK && calls==before); /* Retain original, never failed output. */
        wifi_radio_operation_unlock();assert(esp32_mquickjs_wifi_radio_restart_snapshot_bytes()==sizeof(frozen));wifi_radio_operation_lock();
        new_driver();
        assert(wifi_radio_restart_configs_replay_locked(&token)==ESP_OK && calls==replay_calls && !nvs_writes && native_storage==WIFI_STORAGE_RAM && native_mode==WIFI_MODE_STA);
        assert(rate_writes[0]==1 && native_rates[0].rate==WIFI_PHY_RATE_6M && s_tx_rates.records[0].known);
        if(CONFIG_ESP_WIFI_SOFTAP_SUPPORT)assert(rate_writes[1]==1 && native_rates[1].rate==WIFI_PHY_RATE_12M);
        assert(native_bandwidths[0].ghz_2g==WIFI_BW40 && native_protocols[0].ghz_2g==(WIFI_PROTOCOL_11B|WIFI_PROTOCOL_11G|WIFI_PROTOCOL_11N));
        assert(native_interval==300 && s_interval.known && !s_interval.uncertain);
        assert(mac_writes==(CONFIG_ESP_WIFI_SOFTAP_SUPPORT ? 3U : 1U));
        assert(!memcmp(native_mac[0],frozen.mac[0],6));
        if(CONFIG_ESP_WIFI_SOFTAP_SUPPORT)assert(!memcmp(native_mac[1],frozen.mac[1],6));
        assert(native_ps==WIFI_PS_MAX_MODEM && native_event_mask==0 && native_country.max_tx_power==20);
        assert(wifi_radio_country_equal(&native_country,&frozen.country,false));
        s_radio.started=true;s_radio.driver_state=ESP32_MQUICKJS_WIFI_RADIO_STARTED;
        assert(wifi_radio_restart_configs_verify_locked(&token,WIFI_MODE_STA)==ESP_ERR_INVALID_STATE);
        assert(wifi_radio_restart_configs_post_start_locked(&token)==ESP_OK && native_tx_power==52 && power_writes==1);
        assert(wifi_radio_restart_configs_verify_locked(&token,WIFI_MODE_STA)==ESP_OK);
        assert(!memcmp(native_config[0].sta.password,"secret-sta",10) && native_config[0].sta.pmf_cfg.required);
#if CONFIG_ESP_WIFI_SOFTAP_SUPPORT
        assert(!memcmp(native_config[1].ap.password,"secret-ap",9) && native_config[1].ap.pmf_cfg.required);
#endif
        corrupt_config=true;
        assert(wifi_radio_restart_configs_verify_locked(&token,WIFI_MODE_STA)==ESP_ERR_INVALID_RESPONSE && allocation && !frees);
        for(size_t i=0;i<sizeof(frozen.scratch);++i)assert(((unsigned char *)&s_config_restart.snapshot->scratch)[i]==0);
        esp32_mquickjs_wifi_radio_lifecycle_t stale=token;stale.identity++;
        wifi_radio_restart_configs_discard_locked(&stale);assert(allocation);
        wifi_radio_operation_unlock();cleanup_error=77;
        assert(esp32_mquickjs_wifi_radio_finish_lifecycle(&token,true)==77 && allocation && token.identity==41);
        cleanup_error=0;assert(esp32_mquickjs_wifi_radio_finish_lifecycle(&token,true)==ESP_OK && !allocation && !token.identity && frees==1);
        assert(!esp32_mquickjs_wifi_radio_restart_snapshot_bytes());
    }
    for(unsigned invalid=0;invalid<6;++invalid) {
        if(invalid==4 && !CONFIG_ESP_WIFI_SOFTAP_SUPPORT)continue;
        setup();wifi_radio_operation_lock();
        if(invalid==0)native_country.nchan=0;
        if(invalid==1)native_ps=(wifi_ps_type_t)99;
        if(invalid==2)native_event_mask=2;
        if(invalid==3)native_mac[0][0]|=1;
        if(invalid==4)memcpy(native_mac[1],native_mac[0],6);
        if(invalid==5)native_tx_power=0;
        assert(wifi_radio_restart_configs_capture_locked(&token,WIFI_MODE_STA)==ESP_ERR_INVALID_RESPONSE);
        assert(!writes && !allocation && frees==1);wifi_radio_operation_unlock();
    }
    for(unsigned invalid=0;invalid<6;++invalid) {
        if(invalid==4 && !CONFIG_ESP_WIFI_SOFTAP_SUPPORT)continue;
        setup();wifi_radio_operation_lock();
        assert(wifi_radio_restart_configs_capture_locked(&token,WIFI_MODE_STA)==ESP_OK);
        new_driver();assert(wifi_radio_restart_configs_replay_locked(&token)==ESP_OK);
        s_radio.started=true;s_radio.driver_state=ESP32_MQUICKJS_WIFI_RADIO_STARTED;
        assert(wifi_radio_restart_configs_post_start_locked(&token)==ESP_OK);
        if(invalid==0)native_country.nchan=10;
        if(invalid==1)native_ps=WIFI_PS_NONE;
        if(invalid==2)native_event_mask=1;
        if(invalid==3)native_mac[0][5]^=32;
        if(invalid==4)native_mac[1][5]^=32;
        if(invalid==5)native_tx_power=80;
        assert(wifi_radio_restart_configs_verify_locked(&token,WIFI_MODE_STA)==ESP_ERR_INVALID_RESPONSE);
        assert(allocation && !frees);wipe();wifi_radio_operation_unlock();
    }
    for(unsigned invalid=0;invalid<5;++invalid) {
        setup();wifi_radio_operation_lock();
        if(invalid==0)s_interval.known=false;
        if(invalid==1)s_interval.uncertain=true;
        if(invalid==2)s_interval.generation++;
        if(invalid==3){s_interval.owner.identity=41;s_interval.restore_pending=true;}
        if(invalid==4)s_interval.revision=UINT32_MAX-1;
        assert(wifi_radio_restart_configs_capture_locked(&token,WIFI_MODE_STA)==ESP_ERR_INVALID_STATE);
        assert(!calls && !writes && !allocation && frees==1);wifi_radio_operation_unlock();
    }
    setup();wifi_radio_operation_lock();
    assert(wifi_radio_restart_configs_capture_locked(&token,WIFI_MODE_STA)==ESP_OK);
    new_driver();assert(wifi_radio_restart_configs_replay_locked(&token)==ESP_OK);
    s_radio.started=true;s_radio.driver_state=ESP32_MQUICKJS_WIFI_RADIO_STARTED;
    assert(wifi_radio_restart_configs_post_start_locked(&token)==ESP_OK);
    ++s_interval.revision; /* Same scalar without the accepted replay revision is insufficient. */
    assert(wifi_radio_restart_configs_verify_locked(&token,WIFI_MODE_STA)==ESP_ERR_INVALID_STATE && allocation);
    s_interval.revision=UINT32_MAX-1;unsigned before=calls;
    assert(wifi_radio_restart_configs_capture_locked(&token,WIFI_MODE_STA)==ESP_ERR_INVALID_STATE && allocation && calls==before);
    wipe();wifi_radio_operation_unlock();
    for(unsigned invalid=0;invalid<5;++invalid) {
        setup();wifi_radio_operation_lock();
        if(invalid==0)s_tx_rates.records[0].known=false;
        if(invalid==1)s_tx_rates.records[0].uncertain=true;
        if(invalid==2)s_tx_rates.records[0].generation++;
        if(invalid==3)s_tx_rates.next_identity=0;
        if(invalid==4)s_tx_rate_lease.restore_pending=true;
        assert(wifi_radio_restart_configs_capture_locked(&token,WIFI_MODE_STA)==ESP_ERR_INVALID_STATE);
        assert(!calls && !writes && !allocation && frees==(invalid==4 ? 0U : 1U));wifi_radio_operation_unlock();
    }
    setup();wifi_radio_operation_lock();
    assert(wifi_radio_restart_configs_capture_locked(&token,WIFI_MODE_STA)==ESP_OK);
    new_driver();assert(wifi_radio_restart_configs_replay_locked(&token)==ESP_OK);
    s_radio.started=true;s_radio.driver_state=ESP32_MQUICKJS_WIFI_RADIO_STARTED;
    assert(wifi_radio_restart_configs_post_start_locked(&token)==ESP_OK);
    ++s_tx_rates.records[0].write_identity;
    assert(wifi_radio_restart_configs_verify_locked(&token,WIFI_MODE_STA)==ESP_ERR_INVALID_STATE && allocation);
    s_tx_rates.next_identity=0;before=calls;
    assert(wifi_radio_restart_configs_capture_locked(&token,WIFI_MODE_STA)==ESP_ERR_INVALID_STATE && calls==before && allocation);
    wipe();wifi_radio_operation_unlock();
    for(unsigned invalid=0;invalid<3;++invalid) {
        setup();wifi_radio_operation_lock();
        if(invalid==0)native_protocols[0].ghz_2g=0;
        if(invalid==1)native_bandwidths[0].ghz_2g=(wifi_bandwidth_t)99;
        if(invalid==2)native_band_mode=(wifi_band_mode_t)99;
        assert(wifi_radio_restart_configs_capture_locked(&token,WIFI_MODE_STA)==
            ESP_ERR_INVALID_RESPONSE);
        assert(!writes && !allocation && frees==1);wifi_radio_operation_unlock();
    }
    setup();wifi_radio_operation_lock();
    assert(wifi_radio_restart_configs_capture_locked(&token,WIFI_MODE_STA)==ESP_OK);
    new_driver();assert(wifi_radio_restart_configs_replay_locked(&token)==ESP_OK);
    s_radio.started=true;s_radio.driver_state=ESP32_MQUICKJS_WIFI_RADIO_STARTED;
    assert(wifi_radio_restart_configs_post_start_locked(&token)==ESP_OK);
    native_bandwidths[0].ghz_2g=WIFI_BW20;
    assert(wifi_radio_restart_configs_verify_locked(&token,WIFI_MODE_STA)==ESP_ERR_INVALID_RESPONSE && allocation);
    wipe();wifi_radio_operation_unlock();
#if CONFIG_SOC_WIFI_SUPPORT_5G
    setup();wifi_radio_operation_lock();
    assert(wifi_radio_restart_configs_capture_locked(&token,WIFI_MODE_STA)==ESP_OK);
    new_driver();native_band_mode=WIFI_BAND_MODE_2G_ONLY;
    assert(wifi_radio_restart_configs_replay_locked(&token)==ESP_OK && allocation);
    assert(native_starts==1 && native_stops==1 && !native_running && !s_radio.started && !s_radio.stop_required);
    assert(native_mode==WIFI_MODE_STA && native_band_mode==WIFI_BAND_MODE_AUTO);
    wipe();wifi_radio_operation_unlock();
#endif
#if CONFIG_SOC_WIFI_SUPPORT_5G
    setup();wifi_radio_operation_lock();
    assert(wifi_radio_restart_configs_capture_locked(&token,WIFI_MODE_STA)==ESP_OK);
    new_driver();native_band_mode=WIFI_BAND_MODE_2G_ONLY;
    const char *prepare_stage=NULL;
    assert(wifi_radio_restart_band_prepare_locked(&token,(wifi_mode_t)s_config_restart.snapshot->mask,&prepare_stage)==ESP_OK);
    unsigned prepare_calls=calls;assert(native_starts==1 && native_stops==1 && !native_running);
    wipe();wifi_radio_operation_unlock();
    for(unsigned failure=1;failure<=prepare_calls;++failure) {
        setup();wifi_radio_operation_lock();
        assert(wifi_radio_restart_configs_capture_locked(&token,WIFI_MODE_STA)==ESP_OK);
        wifi_radio_restart_configs_t frozen=*s_config_restart.snapshot;
        new_driver();native_band_mode=WIFI_BAND_MODE_5G_ONLY;fail_at=failure;
        assert(wifi_radio_restart_band_prepare_locked(&token,(wifi_mode_t)s_config_restart.snapshot->mask,&prepare_stage)==77);
        assert(allocation && token.identity==41 && !memcmp(&frozen,s_config_restart.snapshot,sizeof(frozen)));
        for(unsigned i=0;i<WIFI_RADIO_MAX_LEASES;++i)assert(!s_radio.leases[i].identity);
        new_driver();native_band_mode=WIFI_BAND_MODE_2G_ONLY;
        assert(wifi_radio_restart_configs_replay_locked(&token)==ESP_OK);
        assert(native_starts==1 && native_stops==1 && !native_running && !s_radio.stop_required);
        wipe();wifi_radio_operation_unlock();
    }
#endif
    /* Merely clearing started is not completed STOP: stop_required still rejects. */
    setup();wifi_radio_operation_lock();s_radio.started=false;
    assert(wifi_radio_restart_configs_capture_locked(&token,WIFI_MODE_STA)==ESP_ERR_INVALID_STATE);
    assert(!writes && !allocation && !allocations && !frees);wifi_radio_operation_unlock();
    for(unsigned failure=1;failure<=3;++failure) {
        setup();wifi_radio_operation_lock();
        assert(wifi_radio_restart_configs_capture_locked(&token,WIFI_MODE_STA)==ESP_OK);
        wifi_radio_restart_configs_t frozen=*s_config_restart.snapshot;
        new_driver();assert(wifi_radio_restart_configs_replay_locked(&token)==ESP_OK);
        assert(wifi_radio_restart_configs_post_start_locked(&token)==ESP_ERR_INVALID_STATE && !power_writes);
        s_radio.started=true;s_radio.driver_state=ESP32_MQUICKJS_WIFI_RADIO_STARTED;
        if(failure<3)fail_at=calls+5+failure;else clamp_tx_power=true; /* Channel write + four getters precede power. */
        assert(wifi_radio_restart_configs_post_start_locked(&token)==(failure<3 ? 77 : ESP_ERR_INVALID_RESPONSE));
        assert(allocation && !frees && !memcmp(&frozen,s_config_restart.snapshot,sizeof(frozen)));
        assert(s_config_restart.start_phase==(failure==1 ? RESTART_START_POWER : RESTART_START_POWER_READ));
        unsigned before=calls;assert(wifi_radio_restart_configs_post_start_locked(&token)==ESP_ERR_INVALID_STATE && calls==before);
        new_driver();clamp_tx_power=false;
        assert(wifi_radio_restart_configs_replay_locked(&token)==ESP_OK && s_config_restart.start_phase==RESTART_START_CHANNEL);
        s_radio.started=true;s_radio.driver_state=ESP32_MQUICKJS_WIFI_RADIO_STARTED;
        assert(wifi_radio_restart_configs_post_start_locked(&token)==ESP_OK && native_tx_power==52);
        before=calls;assert(wifi_radio_restart_configs_post_start_locked(&token)==ESP_OK && calls==before);
        assert(wifi_radio_restart_configs_verify_locked(&token,WIFI_MODE_STA)==ESP_OK);
        wipe();wifi_radio_operation_unlock();
    }
    setup();wifi_radio_operation_lock();s_radio.started=s_radio.stop_required=native_running=false;s_radio.driver_state=ESP32_MQUICKJS_WIFI_RADIO_STOPPED;
    s_radio.driver_owned=s_radio.storage_configured=false;
    assert(wifi_radio_restart_configs_capture_locked(&token,WIFI_MODE_STA)==ESP_OK && !allocation && !allocations && !calls);
    s_radio.driver_owned=s_radio.storage_configured=true;
    assert(wifi_radio_restart_configs_replay_locked(&token)==ESP_OK && wifi_radio_restart_configs_ready_locked(&token,WIFI_MODE_STA));
    assert(wifi_radio_restart_configs_verify_locked(&token,WIFI_MODE_STA)==ESP_OK);wipe();wifi_radio_operation_unlock();
    assert(!locks && !critical && !helper_locks);return 0;
}
