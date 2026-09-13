
static void policy_reset(bool stopped) {
    reset();memset(&s_policies,0,sizeof(s_policies));native_dynamic=native_11b[0]=native_11b[1]=native_coex=false;
    if(stopped) {
        s_radio.started=false;s_radio.driver_state=ESP32_MQUICKJS_WIFI_RADIO_STOPPED;
        memset(s_radio.leases,0,sizeof(s_radio.leases));s_wifi_application.acquired=false;s_wifi_state.radio_lease.acquired=false;s_ap_lease.acquired=false;
    }
}
