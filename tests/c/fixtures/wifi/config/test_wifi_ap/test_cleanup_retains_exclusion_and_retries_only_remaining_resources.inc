
int main(void) {
    deauth_pending=true;s_ap_netif=(void *)1;s_ap_lease.acquired=true;
    assert(wifi_ap_cleanup()==ESP_ERR_INVALID_STATE&&!releases&&!freed&&!calls[1]);
    deauth_pending=false;
    for(int stage=1;stage<=4;stage++) {
        for(int i=0;i<6;i++)calls[i]=0;
        freed=releases=0;s_ap_netif=(void *)1;s_ap_lease.acquired=true;failed=stage;
        assert(wifi_ap_cleanup()==-stage && s_ap_cleanup_pending);
        assert(freed==(stage==4));
        if(stage==3) {
            assert(wifi_ap_cleanup()==-3 && calls[3]==1 && !freed);
            /* Simulate a device reboot only for the next independent test. */
            s_ap_detach_error=0;s_ap_lease.acquired=false;s_ap_netif=NULL;s_ap_lifecycle.identity=0;
            continue;
        }
        failed=0;assert(wifi_ap_cleanup()==0);
        assert(freed==1 && releases==1 && !s_ap_lifecycle.identity && !s_ap_cleanup_pending);
        assert(calls[1]==1+(stage==1));
        assert(calls[3]==1+(stage==3));
        assert(calls[4]==1+(stage==4));
        assert(wifi_ap_cleanup()==0 && freed==1 && releases==1);
    }
}
