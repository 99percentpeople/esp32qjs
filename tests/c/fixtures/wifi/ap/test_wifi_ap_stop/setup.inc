
static void setup(void) {
    memset(&s_wifi_state,0,sizeof(s_wifi_state));
    s_wifi_application=(esp32_mquickjs_wifi_radio_lease_t){9,1,true};
    s_wifi_state.radio_lease=(esp32_mquickjs_wifi_radio_lease_t){9,2,true};
    s_ap_lease=(esp32_mquickjs_wifi_radio_lease_t){9,3,true};
    s_ap_netif=(void *)1;s_wifi_state.status.connected=true;running=sta_storage=true;
    partial_fail=partial_admissions=partial_quiesce_calls=partial_mutations=adoptions=0;
    partial_quiesced=partial_submitted=false;
    begin_calls=owner_releases=stop_calls=ap_calls=sta_calls=shutdown_calls=fail_at=0;
}
