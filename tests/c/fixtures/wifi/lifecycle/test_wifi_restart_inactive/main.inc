
static void source(wifi_mode_t mode,bool stopped) {
    setup();wifi_radio_operation_lock();
    native_mode=s_radio.effective_mode=mode;native_config[1].ap.channel=1;
    if(stopped) {
        s_radio.event_identity=51;
        wifi_radio_capture_stop_snapshot_locked();
        assert(s_stop_snapshot.error==ESP_OK);
        assert(wifi_radio_stop_locked()==ESP_OK);
        assert(wifi_radio_stop_snapshot_unchanged_locked());
        /* Deliberately unusable stopped SDK outputs cannot replace history. */
        inactive[0]=inactive[1]=0;
    }
    calls=writes=0;memset(inactive_reads,0,sizeof(inactive_reads));
}
static void rebuild_and_activate(void) {
    new_driver();assert(wifi_radio_restart_configs_replay_locked(&token)==ESP_OK);
    assert(!inactive_reads[0] && !inactive_reads[1] && !inactive_writes[0] && !inactive_writes[1]);
    assert(wifi_radio_restart_configs_pre_start_locked(&token,s_config_restart.mode)==ESP_OK);
    /* Physical START/event delivery is the boundary for this post-start unit. */
    s_radio.started=s_radio.stop_required=native_running=true;
    s_radio.driver_state=ESP32_MQUICKJS_WIFI_RADIO_STARTED;
}
static void restored(wifi_mode_t mode) {
    assert(inactive[0]==((mode&WIFI_MODE_STA)?17:6));
    assert(inactive[1]==((mode&WIFI_MODE_AP)?601:300));
    assert(inactive_writes[0]==((mode&WIFI_MODE_STA)?1U:0U));
    assert(inactive_writes[1]==((mode&WIFI_MODE_AP)?1U:0U));
    assert(!inactive_nvs_writes && !nvs_writes);
}
int main(void) {
    const wifi_mode_t modes[]={WIFI_MODE_STA,WIFI_MODE_AP,WIFI_MODE_APSTA};
    for(unsigned mi=0;mi<(CONFIG_ESP_WIFI_SOFTAP_SUPPORT?3U:1U);++mi) {
        wifi_mode_t mode=modes[mi];
        for(unsigned stopped=0;stopped<2;++stopped) {
            source(mode,stopped);
            assert(wifi_radio_restart_configs_capture_locked(&token,mode)==ESP_OK);
            unsigned capture_calls=calls;
            wifi_radio_restart_configs_t frozen=*s_config_restart.snapshot;
            assert(frozen.inactive_mask==mode);
            assert(frozen.inactive_time[0]==((mode&WIFI_MODE_STA)?17:0));
            assert(frozen.inactive_time[1]==((mode&WIFI_MODE_AP)?601:0));
            if(stopped)assert(!inactive_reads[0] && !inactive_reads[1]);
            rebuild_and_activate();calls=writes=0;
            assert(wifi_radio_restart_configs_post_start_locked(&token)==ESP_OK);
            unsigned post_calls=calls;
            restored(mode);
            assert(wifi_radio_restart_configs_commit_locked(&token,mode)==ESP_OK);
            assert(s_radio.storage==WIFI_STORAGE_FLASH && !inactive_nvs_writes);
            assert(!memcmp(&frozen,s_config_restart.snapshot,sizeof(frozen)));
            wipe();wifi_radio_operation_unlock();

            for(unsigned failure=1;failure<=capture_calls;++failure) {
                source(mode,stopped);fail_at=failure;
                assert(wifi_radio_restart_configs_capture_locked(&token,mode)==77);
                assert(!allocation && !writes && !s_radio.fault_stage);
                wifi_radio_operation_unlock();
            }
            for(unsigned failure=1;failure<=post_calls;++failure) {
                source(mode,stopped);
                assert(wifi_radio_restart_configs_capture_locked(&token,mode)==ESP_OK);
                frozen=*s_config_restart.snapshot;
                rebuild_and_activate();calls=writes=0;fail_at=failure;
                assert(wifi_radio_restart_configs_post_start_locked(&token)==77);
                assert(allocation && !inactive_nvs_writes && s_radio.fault_stage);
                assert(!memcmp(&frozen,s_config_restart.snapshot,sizeof(frozen)));
                unsigned before=calls;
                assert(wifi_radio_restart_configs_post_start_locked(&token)==ESP_ERR_INVALID_STATE && calls==before);
                rebuild_and_activate();
                assert(wifi_radio_restart_configs_post_start_locked(&token)==ESP_OK);
                restored(mode);
                assert(wifi_radio_restart_configs_commit_locked(&token,mode)==ESP_OK);
                wipe();wifi_radio_operation_unlock();
            }
        }
        source(mode,false);
        assert(wifi_radio_restart_configs_capture_locked(&token,mode)==ESP_OK);
        rebuild_and_activate();corrupt_inactive=true;
        assert(wifi_radio_restart_configs_post_start_locked(&token)==ESP_ERR_INVALID_RESPONSE);
        assert(!strcmp(s_radio.configuration.stage,(mode&WIFI_MODE_STA)?
            "restart-station-inactive-readback":"restart-ap-inactive-readback"));
        assert(!inactive_nvs_writes && allocation);wipe();wifi_radio_operation_unlock();

        source(mode,false);
        assert(wifi_radio_restart_configs_capture_locked(&token,mode)==ESP_OK);
        rebuild_and_activate();
        assert(wifi_radio_restart_configs_post_start_locked(&token)==ESP_OK);
        inactive[(mode&WIFI_MODE_STA)?0:1]++;
        assert(wifi_radio_restart_configs_commit_locked(&token,mode)==ESP_ERR_INVALID_RESPONSE);
        assert(s_radio.storage==WIFI_STORAGE_RAM && allocation && !inactive_nvs_writes);
        wipe();wifi_radio_operation_unlock();
    }
    source(WIFI_MODE_STA,false);inactive[0]=2;
    assert(wifi_radio_restart_configs_capture_locked(&token,WIFI_MODE_STA)==ESP_ERR_INVALID_RESPONSE);
    assert(!allocation && !writes);wifi_radio_operation_unlock();
#if CONFIG_ESP_WIFI_SOFTAP_SUPPORT
    source(WIFI_MODE_APSTA,false);inactive[1]=9;
    assert(wifi_radio_restart_configs_capture_locked(&token,WIFI_MODE_APSTA)==ESP_ERR_INVALID_RESPONSE);
    assert(!allocation && !writes);wifi_radio_operation_unlock();
    source(WIFI_MODE_APSTA,false);
    assert(wifi_radio_restart_configs_capture_locked(&token,WIFI_MODE_STA)==ESP_OK);
    assert(s_config_restart.snapshot->inactive_saved_mask==WIFI_MODE_APSTA && !writes);
    wipe();
    wifi_radio_operation_unlock();
#endif
    assert(!allocation && !locks && !critical && !helper_locks);
    return 0;
}
