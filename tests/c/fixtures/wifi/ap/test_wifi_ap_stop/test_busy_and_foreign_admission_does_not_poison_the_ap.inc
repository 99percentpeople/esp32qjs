
int main(void) {
    setup();
    bool *busy[]={&s_wifi_state.scan_in_progress,&s_wifi_state.scan_draining,
        &s_wifi_state.scan_results_pending,&s_wifi_state.scan_start_active,&s_wifi_state.scan_stop_active,
        &s_wifi_state.connect_in_progress,&s_wifi_state.connect_draining,&s_wifi_state.connect_start_active,
        &s_wifi_state.disconnect_active,&s_wifi_state.scan_future_registered,&s_wifi_state.connect_future_registered,
        &s_wifi_state.runtime_cleanup_pending};
    for(unsigned i=0;i<sizeof(busy)/sizeof(*busy);i++) {
        *busy[i]=true;assert(js_wifi_stop_ap(NULL,NULL,0,NULL)==-99);*busy[i]=false;
        assert(!s_ap_cleanup_pending && !s_wifi_ap_stop_cleanup && !s_wifi_lifecycle.identity);
        assert(!partial_admissions && !owner_releases && s_ap_netif && s_ap_lease.acquired);
    }
    foreign_owner=true;assert(js_wifi_stop_ap(NULL,NULL,0,NULL)==-99);
    assert(!s_ap_cleanup_pending && !s_wifi_ap_stop_cleanup && !s_wifi_lifecycle.identity && s_ap_lease.acquired);
    foreign_owner=false;
    assert(js_wifi_stop_ap(NULL,NULL,2,NULL)==-98 && partial_admissions==1);
    assert(js_wifi_stop_ap(NULL,NULL,0,NULL)==123 && partial_admissions==2);
}
