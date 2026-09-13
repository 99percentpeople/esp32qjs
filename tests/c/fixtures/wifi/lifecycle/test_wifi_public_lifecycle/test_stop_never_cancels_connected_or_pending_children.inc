
int main(void) {
    bool *busy[]={&s_wifi_state.status.connected,&s_wifi_state.connect_in_progress,
        &s_wifi_state.connect_draining,&s_wifi_state.connect_start_active,&s_wifi_state.disconnect_active,
        &s_wifi_state.scan_in_progress,&s_wifi_state.scan_draining,&s_wifi_state.scan_results_pending,
        &s_wifi_state.scan_start_active,&s_wifi_state.scan_stop_active,
        &s_wifi_state.scan_future_registered,&s_wifi_state.connect_future_registered};
    for(unsigned i=0;i<sizeof(busy)/sizeof(*busy);i++) {
        *busy[i]=true;
        assert(js_wifi_stop(NULL,NULL,0,NULL)==-99);
        assert(!begin_calls && !cleanup_calls && !disconnect_calls && !stops);
        *busy[i]=false;
    }
}
