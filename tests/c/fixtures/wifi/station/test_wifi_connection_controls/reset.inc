
static void reset(void) {
    assert(!locks && !critical && !helper_locks);
    memset(&s_radio,0,sizeof(s_radio));memset(&s_wifi_state,0,sizeof(s_wifi_state));memset(&s_ap_lease,0,sizeof(s_ap_lease));
    memset(&s_wifi_lifecycle,0,sizeof(s_wifi_lifecycle));memset(&result,0,sizeof(result));
    memset(&s_inactive_history,0,sizeof(s_inactive_history));
    memset(&s_rssi_request,0,sizeof(s_rssi_request));
    s_wifi_configuration_cleanup=s_wifi_ap_stop_cleanup=s_ap_cleanup_pending=corrupt_readback=false;
    s_tx_rate_lease.identity=0;s_radio.generation=7;
    s_radio.driver_owned=s_radio.storage_configured=s_radio.started=true;
    s_radio.storage=WIFI_STORAGE_RAM;
    s_radio.driver_state=ESP32_MQUICKJS_WIFI_RADIO_STARTED;s_radio.effective_mode=WIFI_MODE_STA;
    s_wifi_application=(esp32_mquickjs_wifi_radio_lease_t){7,11,ESP32_MQUICKJS_WIFI_RADIO_CLIENT_APPLICATION,true};
    s_wifi_state.radio_lease=(esp32_mquickjs_wifi_radio_lease_t){7,12,ESP32_MQUICKJS_WIFI_RADIO_CLIENT_WIFI_STA,true};
    s_radio.leases[0]=(wifi_radio_live_lease_t){11,s_wifi_application.client};
    s_radio.leases[1]=(wifi_radio_live_lease_t){12,s_wifi_state.radio_lease.client};
#if CONFIG_ESP_WIFI_SOFTAP_SUPPORT
    s_ap_lease=(esp32_mquickjs_wifi_radio_lease_t){7,13,ESP32_MQUICKJS_WIFI_RADIO_CLIENT_WIFI_AP,true};
    s_radio.leases[2]=(wifi_radio_live_lease_t){13,s_ap_lease.client};s_radio.effective_mode=WIFI_MODE_APSTA;
#endif
    inactive[0]=6;inactive[1]=300;threshold=-60;calls=writes=fail_at=rollback_fail=0;
}
