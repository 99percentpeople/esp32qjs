
int main(void){
    setup();assert(initialize_suffix()==0);
    assert(netif.stack.input==tcpip_input && calls==4 && published==1 && s_wifi_state.started);
    for(fail_at=1;fail_at<=4;fail_at++){
        setup();assert(initialize_suffix()==77);
        assert(!netif.stack.input && calls==fail_at && !published && !s_wifi_state.started && !s_wifi_state.status.started);
    }
    fail_at=0;setup();netif.stack.input=tcpip_input;
    assert(initialize_suffix()==0 && !calls && published==1 && fenced==1); /* real START already initialized it */
    setup();start_on_fence=true;
    assert(initialize_suffix()==0 && !calls && published==1 && fenced==1); /* real START completes while joining its fence */
    setup();fence_error=78;
    assert(initialize_suffix()==78 && !calls && !published && !netif.stack.input);
    setup();observed.started=false;
    assert(initialize_suffix()==0 && !calls && !published && !netif.stack.input && !fenced);
    setup();observed.mode=WIFI_MODE_AP;
    assert(initialize_suffix()==0 && !calls && !published && !netif.stack.input && !fenced);
    setup();observed.mode=WIFI_MODE_STA|WIFI_MODE_AP;
    assert(initialize_suffix()==0 && calls==4 && published==1 && netif.stack.input);
    return 0;
}
