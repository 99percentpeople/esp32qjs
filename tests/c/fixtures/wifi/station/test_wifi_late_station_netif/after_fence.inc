
int main(void){
    /* Another owner's esp_wifi_start has emitted START before our default
     * handler was attached, but has not returned: the early snapshot is false.
     * Radio then joins that native call and its event fence. */
    setup();observed.started=false;
    assert(esp32_mquickjs_wifi_ensure_started()==ESP_OK);
    assert(netif.stack.input==tcpip_input && calls==4 && published==1);
    assert(s_wifi_setup_stage==NULL && s_wifi_setup_error==ESP_OK && !cleaned);
    for(fail_at=1;fail_at<=4;fail_at++){
        setup();observed.started=false;
        assert(esp32_mquickjs_wifi_ensure_started()==77);
        assert(cleaned==1 && s_wifi_setup_error==77 && !published);
    }
    fail_at=0;setup();start_on_fence=true;
    assert(esp32_mquickjs_wifi_ensure_started()==ESP_OK && !calls && published==1);
    return 0;
}
