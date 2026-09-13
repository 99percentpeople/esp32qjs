
static void reset(void) {
    assert(!live && !gWpsSm && !adopted);
    memset(&record,0,sizeof(record));nth=fail_nth=0;
    mac_error=device_error=pin_error=protocol_null=callback_error=0;
    probe_oom=resize_error=assoc_oom=appie_error=0;
    current_status=status_error=config_error=connect_error=0;
    config_calls=connect_calls=timers=tx_frees=0;
    associated_error=tx_error=parse_error=0;tx_oom=false;
}
static void retire_fixture_sdk(void) {
    esp_wifi_set_wps_cb_internal(NULL);
    record.status.inputs_stopped=true;
    if(gWpsSm)assert(wifi_station_wps_deinit()==ESP_OK);
    assert(!live && !s_wps_sm_cb);
}
