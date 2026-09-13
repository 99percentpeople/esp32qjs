
int main(void) {
    esp32_mquickjs_wifi_radio_configuration_selection_t request={.start_only=true};
    expect_no_config_inputs=true;
    assert(wifi_configure_selected_interfaces(&request,NULL,NULL,NULL,NULL,NULL,NULL,NULL)==0);
    assert(request.mode==WIFI_MODE_APSTA && request.storage==WIFI_STORAGE_FLASH);
    assert(calls[7]==1 && calls[8]==1 && calls[10]==1 && calls[11]==1 && stored_ap_reads==1);
    assert(s_wifi_application==1 && s_wifi_state.radio_lease==2 && ap_lease==3 && !live_allocation);
    assert(!s_wifi_configuration_cleanup && !s_wifi_lifecycle.identity);
    reset_fixture();start_already_running=true;
    assert(wifi_configure_selected_interfaces(&request,NULL,NULL,NULL,NULL,NULL,NULL,NULL)==0);
    assert(running_starts==1 && !s_wifi_lifecycle.identity && !s_wifi_configuration_cleanup);
    for(int i=0;i<12;i++)assert(!calls[i]);
    start_already_running=false;
    for(int fail=0;fail<3;fail++) {
        reset_fixture();allocation_failure=fail==0;stored_ap_error=fail==1?-77:0;fail_at=fail==2?11:0;
        assert(wifi_configure_selected_interfaces(&request,NULL,NULL,NULL,NULL,NULL,NULL,NULL)!=0);
        assert(s_wifi_configuration_cleanup && s_wifi_lifecycle.identity==7 && !live_allocation);
        if(fail==0)assert(!calls[3] && !strcmp(s_wifi_state.cleanup_stage,"start-ap-allocate"));
        if(fail==1)assert(!calls[11] && !strcmp(s_wifi_state.cleanup_stage,"start-ap-config"));
        assert(wifi_finish_configuration_cleanup()==0);
    }
}
