
int main(void) {
    for(int mode=WIFI_MODE_STA;mode<=WIFI_MODE_APSTA;mode++) {
        reset_fixture();s_radio.driver_state=ESP32_MQUICKJS_WIFI_RADIO_STOPPED;
        s_radio.storage_configured=true;s_radio.effective_mode=mode;s_radio.storage=WIFI_STORAGE_FLASH;
        esp32_mquickjs_wifi_radio_configuration_selection_t request={.start_only=true};
        esp32_mquickjs_wifi_radio_lifecycle_t token={0};bool running=true;
        assert(esp32_mquickjs_wifi_radio_begin_start_lifecycle(NULL,NULL,NULL,&request,&token,&running)==0);
        assert(!running && token.identity==11 && request.mode==mode && request.storage==WIFI_STORAGE_FLASH && request.start);
        assert(!mode_queries && !s_radio.started);
    }
    reset_fixture();esp32_mquickjs_wifi_radio_configuration_selection_t request={.start_only=true};
    esp32_mquickjs_wifi_radio_lifecycle_t token={0};bool running=true;
    assert(esp32_mquickjs_wifi_radio_begin_start_lifecycle(NULL,NULL,NULL,&request,&token,&running)==0);
    assert(!running && request.mode==WIFI_MODE_STA && request.storage==WIFI_STORAGE_RAM);
    reset_fixture();request=(esp32_mquickjs_wifi_radio_configuration_selection_t){.start_only=true};token.identity=0;
    esp32_mquickjs_wifi_radio_lease_t ap={7,6,ESP32_MQUICKJS_WIFI_RADIO_CLIENT_WIFI_AP,true};
    s_radio.leases[0].identity=6;s_radio.leases[0].client=ap.client;change_at_lock=true;
    assert(esp32_mquickjs_wifi_radio_begin_start_lifecycle(NULL,NULL,&ap,&request,&token,&running)==0);
    assert(running && !token.identity && request.mode==WIFI_MODE_AP && request.storage==WIFI_STORAGE_FLASH && ap.acquired);
    assert(s_radio.next_lifecycle_identity==11 && mode_queries==1);
    request.mode_set=true;request.mode=WIFI_MODE_STA;
    assert(esp32_mquickjs_wifi_radio_begin_start_lifecycle(NULL,NULL,&ap,&request,&token,&running)==ESP_ERR_INVALID_STATE);
    assert(!running && !token.identity && mode_queries==1 && ap.acquired);
    request.mode=WIFI_MODE_AP;request.storage_set=true;request.storage=WIFI_STORAGE_RAM;
    assert(esp32_mquickjs_wifi_radio_begin_start_lifecycle(NULL,NULL,&ap,&request,&token,&running)==ESP_ERR_INVALID_STATE && mode_queries==1);
    request.storage=WIFI_STORAGE_FLASH;ap.generation--;
    assert(esp32_mquickjs_wifi_radio_begin_start_lifecycle(NULL,NULL,&ap,&request,&token,&running)==ESP_ERR_INVALID_STATE && mode_queries==1);
}
