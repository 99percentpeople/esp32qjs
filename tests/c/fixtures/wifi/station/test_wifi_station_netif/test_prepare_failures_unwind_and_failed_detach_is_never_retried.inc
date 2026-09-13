
int main(void) {
    for(int stage=1;stage<=3;stage++) {
        memset(&s_wifi_state,0,sizeof(s_wifi_state));memset(calls,0,sizeof(calls));destroyed=0;
        failed=stage;
        assert(wifi_prepare_station_netif()==(stage==1?ESP_ERR_NO_MEM:-stage));
        assert(!strcmp(s_wifi_setup_stage,stage==1?"netif-create":stage==2?"netif-attach":"netif-handlers"));
        for(int step=1;step<=3;step++) assert(calls[step]==(step<=stage));
        failed=0;assert(wifi_retire_station_netif()==ESP_OK);
        assert(!s_wifi_state.sta_netif && calls[4]==(stage>1) && destroyed==(stage>1));
        assert(wifi_retire_station_netif()==ESP_OK && calls[4]==(stage>1));
    }
    memset(&s_wifi_state,0,sizeof(s_wifi_state));memset(calls,0,sizeof(calls));destroyed=0;
    assert(wifi_prepare_station_netif()==ESP_OK);
    failed=4;assert(wifi_retire_station_netif()==-4);
    assert(s_wifi_state.sta_netif && s_wifi_state.sta_detach_error==-4 && !destroyed);
    failed=0;assert(wifi_retire_station_netif()==-4 && calls[4]==1 && !destroyed);
    assert(wifi_prepare_station_netif()==ESP_ERR_INVALID_STATE && calls[1]==1);
    return 0;
}
