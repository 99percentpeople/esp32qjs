
int main(void) {
    wifi_config_t sta={1},ap={2};
    assert(esp32_mquickjs_wifi_configure_interfaces(WIFI_MODE_APSTA,WIFI_STORAGE_RAM,true,
        &sta,accept_config,NULL,NULL,NULL,NULL,false)==ESP_ERR_INVALID_ARG && !calls[1] && !calls[2]);
    controls_invalid=1;
    assert(esp32_mquickjs_wifi_configure_interfaces(WIFI_MODE_STA,WIFI_STORAGE_RAM,true,
        NULL,NULL,NULL,NULL,NULL,NULL,false)==ESP_ERR_INVALID_ARG && !calls[2]);
    controls_invalid=0;start_controls_invalid=1;
    assert(esp32_mquickjs_wifi_configure_interfaces(WIFI_MODE_STA,WIFI_STORAGE_RAM,true,
        NULL,NULL,NULL,NULL,NULL,NULL,false)==ESP_ERR_INVALID_ARG && !calls[2]);
    start_controls_invalid=0;
    for(int failed=1;failed<=11;failed++) {
        reset_fixture();fail_at=failed;
        int expected=failed==9?ESP_ERR_INVALID_STATE:-failed;
        assert(esp32_mquickjs_wifi_configure_interfaces(WIFI_MODE_APSTA,WIFI_STORAGE_RAM,true,
            &sta,accept_config,&ap,accept_config,NULL,NULL,false)==expected);
        for(int step=1;step<=11;step++)assert(calls[step]==(step<=failed));
        if(failed<3)assert(!s_wifi_configuration_cleanup && !s_wifi_lifecycle.identity);
        else {
            assert(s_wifi_configuration_cleanup && s_wifi_configuration_mode==WIFI_MODE_APSTA && s_wifi_lifecycle.identity==7);
            assert(s_wifi_state.runtime_cleanup_pending && s_wifi_state.cleanup_error==expected);
            if(failed==5)assert(!strcmp(s_wifi_state.cleanup_stage,"netif-detach"));
            if(failed==10)assert(!strcmp(s_wifi_state.cleanup_stage,"configuration-commit"));
            assert(!s_wifi_application && !s_wifi_state.radio_lease && !ap_lease);
            int before[12];memcpy(before,calls,sizeof(calls));
            assert(esp32_mquickjs_wifi_configure_interfaces(WIFI_MODE_APSTA,WIFI_STORAGE_RAM,true,
                &sta,accept_config,&ap,accept_config,NULL,NULL,false)==ESP_ERR_INVALID_STATE);
            assert(!memcmp(before,calls,sizeof(calls)));
            assert(esp32_mquickjs_wifi_cleanup_ap_configuration()==ESP_ERR_INVALID_STATE && !cleanup_calls);
            assert(wifi_finish_configuration_cleanup()==ESP_OK);
        }
    }
    for(int mode=1;mode<=3;mode++)for(int start=0;start<=1;start++) {
        reset_fixture();fail_at=0;
        assert(esp32_mquickjs_wifi_configure_interfaces(mode,WIFI_STORAGE_FLASH,start,
            mode&WIFI_MODE_STA?&sta:NULL,mode&WIFI_MODE_STA?accept_config:NULL,
            mode&WIFI_MODE_AP?&ap:NULL,mode&WIFI_MODE_AP?accept_config:NULL,NULL,NULL,false)==ESP_OK);
        assert(!s_wifi_configuration_cleanup && !s_wifi_lifecycle.identity && !s_wifi_state.runtime_cleanup_pending);
        assert(!s_wifi_state.cleanup_stage && !s_wifi_state.cleanup_error);
        assert(calls[7]==(start && (mode&WIFI_MODE_STA)) && calls[8]==(start && (mode&WIFI_MODE_AP)));
        assert(calls[9]==(start && (mode&WIFI_MODE_AP)) && calls[10]==1 && calls[11]==1);
    }
    reset_fixture();fail_at=0;
    /* Starting STA does not implicitly connect and need not replace its config. */
    assert(esp32_mquickjs_wifi_configure_interfaces(WIFI_MODE_STA,WIFI_STORAGE_RAM,true,
        NULL,NULL,NULL,NULL,NULL,NULL,false)==ESP_OK && calls[7]==1 && !calls[8]);
    reset_fixture();fail_at=0;
    int start_controls=7;expected_start_controls=&start_controls;
    assert(esp32_mquickjs_wifi_configure_interfaces(WIFI_MODE_STA,WIFI_STORAGE_RAM,true,
        NULL,NULL,NULL,NULL,NULL,&start_controls,false)==ESP_OK && calls[11]==1);
    expected_start_controls=NULL;
    reset_fixture();expected_allow_disconnect=true;disconnect_boundary_error=-55;
    assert(esp32_mquickjs_wifi_configure_interfaces(WIFI_MODE_STA,WIFI_STORAGE_RAM,true,
        NULL,NULL,NULL,NULL,NULL,NULL,true)==-55 && !calls[3]);
    assert(s_wifi_configuration_cleanup && s_wifi_state.runtime_cleanup_pending &&
        !strcmp(s_wifi_state.cleanup_stage,"configuration-disconnect"));
    disconnect_boundary_error=0;assert(wifi_finish_configuration_cleanup()==ESP_OK);expected_allow_disconnect=false;
    reset_fixture();fail_at=10;
    assert(esp32_mquickjs_wifi_configure_interfaces(WIFI_MODE_AP,WIFI_STORAGE_RAM,true,
        NULL,NULL,&ap,accept_config,NULL,NULL,false)==-10);
    assert(esp32_mquickjs_wifi_cleanup_ap_configuration()==ESP_OK && cleanup_calls==1);
    reset_fixture();fail_at=0;
    assert(esp32_mquickjs_wifi_apply_configuration(NULL,NULL)==ESP_ERR_INVALID_ARG && !calls[2]);
    esp32_mquickjs_wifi_configuration_execution_t execution={0};
    esp32_mquickjs_wifi_configuration_t captured={.station_set=true,.station={9}};
    expected_start_controls=&captured.start_controls;
    assert(esp32_mquickjs_wifi_apply_configuration(&captured,&execution)==ESP_OK && calls[10]==1 && !calls[7]);
    assert(execution.admitted && execution.stop_attempted && execution.configuration_attempted && execution.resume_attempted && !strcmp(execution.stage,"complete"));
    assert(!captured.mode_set && !captured.storage_set && !captured.start_set &&
        captured.mode==WIFI_MODE_STA && captured.storage==WIFI_STORAGE_RAM && !captured.start);
    reset_fixture();fail_at=10;memset(&captured,0,sizeof(captured));
    assert(esp32_mquickjs_wifi_apply_configuration(&captured,&execution)==-10);
    assert(execution.admitted && execution.stop_attempted && execution.configuration_attempted && !execution.resume_attempted && !strcmp(execution.stage,"configuration-commit"));
    assert(captured.mode==0 && captured.storage==0 && !captured.start); /* No successful resolution published. */
    assert(wifi_finish_configuration_cleanup()==ESP_OK);
    return 0;
}
