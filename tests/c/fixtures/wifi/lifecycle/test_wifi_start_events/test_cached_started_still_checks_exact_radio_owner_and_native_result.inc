
int main(void) {
    s_wifi_state.started=s_wifi_state.status.started=true;
    start_error=-11;
    assert(esp32_mquickjs_wifi_ensure_started()==-11 && start_calls==1 && !status_calls && !bits);
    start_error=0;status_error=-12;
    assert(esp32_mquickjs_wifi_ensure_started()==-12 && start_calls==2 && status_calls==1 && !bits);
    status_error=0;observed.mode=2;
    assert(esp32_mquickjs_wifi_ensure_started()==ESP_ERR_INVALID_STATE && !bits);
    observed.mode=WIFI_MODE_STA;observed.started=false;
    assert(esp32_mquickjs_wifi_ensure_started()==ESP_ERR_INVALID_STATE && !bits);
    assert(!netif_calls && !cleanup_calls);
    observed.started=true;
    assert(esp32_mquickjs_wifi_ensure_started()==ESP_OK && bits==WIFI_STARTED_BIT);
    assert(netif_calls==1 && !cleanup_calls && !s_wifi_setup_error && !s_wifi_setup_stage);
    init_error=-13;int starts=start_calls;
    assert(esp32_mquickjs_wifi_ensure_started()==-13 && start_calls==starts);
    init_error=0;bits=0;netif_error=-14;
    assert(esp32_mquickjs_wifi_ensure_started()==-14 && netif_calls==2 && cleanup_calls==1);
    assert(!bits && s_wifi_setup_error==-14 && s_wifi_setup_stage);
    return 0;
}
