
int main(void) {
    JSValue options=1;
    for(int stage=1;stage<=3;stage++) {
        fail_at=stage;reserves=configures=cleanups=zeroed=0;
        assert(js_wifi_start_ap(NULL,NULL,1,&options)==-99 && zeroed==1);
        assert(reserves==1 && configures==(stage>1) && cleanups==(stage==2));
        if(stage==3)assert(pending && !s_ap_lease.acquired && !strcmp(s_ap_stage,"configuration-commit"));
        else assert(!pending && !s_ap_lease.acquired);
    }
    pending=false;state.cleanup_stage=NULL;fail_at=0;
    assert(js_wifi_start_ap(NULL,NULL,1,&options)==123 && s_ap_lease.acquired && s_ap_netif);
    assert(zeroed==2);
    s_ap_lease.acquired=false;s_ap_netif=NULL;
    for(share_mode=1;share_mode<=2;share_mode++) {
        int r=reserves,c=configures,done=cleanups,z=zeroed;
        assert(js_wifi_start_ap(NULL,NULL,1,&options)==(share_mode==1?123:-99));
        assert(reserves==r && configures==c && cleanups==done && zeroed==z+1);
    }
    /* Replacement is reachable only after explicit capture. Failure stays
     * with the coordinator; the adapter never invokes an automatic retry. */
    s_ap_netif=(void *)1;s_ap_lease.acquired=true;
    int r=reserves,c=configures,done=cleanups,z=zeroed;
    assert(js_wifi_start_ap(NULL,NULL,1,&options)==-99 && !activations);
    allow_disconnect=true;
    assert(js_wifi_start_ap(NULL,NULL,1,&options)==123 && activations==1);
    activate_error=-77;
    assert(js_wifi_start_ap(NULL,NULL,1,&options)==-99 && activations==2);
    assert(!strcmp(s_ap_stage,"configuration-resume"));
    assert(reserves==r && configures==c && cleanups==done && zeroed==z+3);
    return 0;
}
