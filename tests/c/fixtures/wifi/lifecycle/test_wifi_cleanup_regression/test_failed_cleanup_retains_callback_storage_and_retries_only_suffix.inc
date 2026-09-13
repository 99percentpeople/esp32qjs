
int main(void) {
    for(int stage=1;stage<=8;stage++) {
        memset(&s_wifi_state,0,sizeof(s_wifi_state));memset(calls,0,sizeof(calls));freed=released=0;
        s_wifi_state.connect_timeout_timer=s_wifi_state.ip_event_instance=s_wifi_state.wifi_scan_event_instance=(void *)1;
        s_wifi_state.wifi_disconnect_event_instance=s_wifi_state.wifi_start_event_instance=s_wifi_state.sta_netif=(void *)1;
        s_wifi_state.lock=s_wifi_state.event_group=s_wifi_state.scan_queue=s_wifi_state.connect_queue=s_wifi_state.driver_event_queue=(void *)1;
        s_wifi_state.radio_lease=1;s_wifi_state.control_event_instance=(void *)1;
        s_wifi_state.scan_generation=s_wifi_state.connect_generation=s_wifi_state.disconnect_epoch=UINT32_MAX;
        s_wifi_state.disconnect_epoch_exhausted=true;
        fail_at=stage;wifi_cleanup_failed_init();
        assert(freed==0 && released==0 && s_wifi_state.lock && s_wifi_state.radio_lease);
        assert(s_wifi_state.cleanup_error==-stage && s_wifi_state.cleanup_stage);
        for(int i=1;i<=8;i++) assert(calls[i]==(i<=stage));
        fail_at=0;wifi_cleanup_failed_init();
        assert(freed==2 && released==1 && !s_wifi_state.lock && !s_wifi_state.cleanup_error);
        assert(s_wifi_state.scan_generation==UINT32_MAX && s_wifi_state.connect_generation==UINT32_MAX);
        assert(s_wifi_state.disconnect_epoch==UINT32_MAX && s_wifi_state.disconnect_epoch_exhausted);
        for(int i=1;i<=8;i++) assert(calls[i]==1+(i==stage));
        wifi_cleanup_failed_init();
        assert(freed==2 && released==1);
    }
    /* A failed quiesce retains netif/queues and excludes new admission. */
    s_wifi_state.lock=s_wifi_state.event_group=s_wifi_state.scan_queue=s_wifi_state.connect_queue=s_wifi_state.driver_event_queue=(void *)1;
    s_wifi_state.sta_netif=(void *)1;s_wifi_state.radio_lease=1;
    s_wifi_state.runtime_cleanup_pending=true;s_wifi_lifecycle.identity=1;
    memset(calls,0,sizeof(calls));freed=released=0;fail_at=9;
    assert(wifi_cleanup_failed_init()==-9);
    assert(!freed && released==1 && s_wifi_lifecycle.identity && !s_wifi_state.radio_lease);
    fail_at=0;assert(wifi_cleanup_failed_init()==0);
    assert(freed==2 && released==1 && calls[8]==1 && calls[9]==2 && calls[10]==1);
    assert(!s_wifi_lifecycle.identity && !s_wifi_state.runtime_cleanup_pending);
    /* stop before first init, plus final reservation release failure/retry. */
    s_wifi_state.runtime_cleanup_pending=true;s_wifi_lifecycle.identity=2;fail_at=10;
    assert(wifi_cleanup_failed_init()==-10 && s_wifi_lifecycle.identity==2);
    fail_at=0;assert(wifi_cleanup_failed_init()==0);
    assert(!s_wifi_state.runtime_cleanup_pending && !s_wifi_lifecycle.identity);
    /* Failed detach has already destroyed the driver; never retry it. */
    memset(&s_wifi_state,0,sizeof(s_wifi_state));memset(calls,0,sizeof(calls));freed=released=0;
    s_wifi_state.lock=s_wifi_state.event_group=s_wifi_state.scan_queue=s_wifi_state.connect_queue=s_wifi_state.driver_event_queue=(void *)1;
    s_wifi_state.sta_netif=(void *)1;s_wifi_state.radio_lease=1;s_wifi_lifecycle.identity=3;fail_at=11;
    assert(wifi_cleanup_failed_init()==-11 && s_wifi_state.sta_detach_error==-11);
    assert(!freed && calls[11]==1 && s_wifi_lifecycle.identity==3);
    fail_at=0;assert(wifi_cleanup_failed_init()==-11 && calls[11]==1 && !freed);
    /* Separate fixture: helper-only retirement must not release caller token. */
    memset(&s_wifi_state,0,sizeof(s_wifi_state));memset(calls,0,sizeof(calls));s_wifi_lifecycle.identity=4;
    assert(wifi_cleanup_helper(false)==0 && s_wifi_lifecycle.identity==4 && !calls[10]);
}
