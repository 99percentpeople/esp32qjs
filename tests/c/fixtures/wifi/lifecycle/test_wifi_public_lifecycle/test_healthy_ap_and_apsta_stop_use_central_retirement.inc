
int main(void) {
    for(int station=0;station<=1;station++) {
        ap_control=true;s_wifi_application.acquired=station;s_wifi_state.radio_lease.acquired=station;
        configuration_admission_denied=true;
        assert(js_wifi_stop(NULL,NULL,0,NULL)==-99 && ap_control && !s_wifi_configuration_cleanup);
        assert(s_wifi_application.acquired==station && !configuration_cleanup_calls && !cleanup_calls);
        configuration_admission_denied=false;s_wifi_state.status.connected=true;
        assert(js_wifi_stop(NULL,NULL,0,NULL)==-99 && ap_control && !s_wifi_configuration_cleanup);
        s_wifi_state.status.connected=false;configuration_cleanup_error=-8;
        assert(js_wifi_stop(NULL,NULL,0,NULL)==-99 && !ap_control && s_wifi_configuration_cleanup);
        assert(s_wifi_configuration_stop_only && s_wifi_configuration_mode==(station?WIFI_MODE_APSTA:WIFI_MODE_AP));
        assert(s_wifi_lifecycle.identity==7 && !begin_calls && !cleanup_calls);
        int admitted=configuration_begin_calls;configuration_cleanup_error=0;
        assert(js_wifi_stop(NULL,NULL,0,NULL)==123 && configuration_begin_calls==admitted);
        assert(!s_wifi_configuration_cleanup && !s_wifi_configuration_stop_only && !s_wifi_lifecycle.identity);
        assert(configuration_cleanup_calls==2 && !disconnect_calls && !stops);
        configuration_cleanup_calls=0;
    }
}
