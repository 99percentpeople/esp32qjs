
int main(void) {
    reset_fixture();s_radio.driver_state=ESP32_MQUICKJS_WIFI_RADIO_STARTED;
    s_radio.storage_configured=true;s_radio.started=s_radio.stop_required=true;
    s_radio.effective_mode=WIFI_MODE_STA;s_radio.storage=WIFI_STORAGE_FLASH;
    s_radio.leases[0]=(wifi_radio_live_lease_t){.identity=9,.client=ESP32_MQUICKJS_WIFI_RADIO_CLIENT_CSI,.required_mode=WIFI_MODE_STA};
    s_radio.clients[ESP32_MQUICKJS_WIFI_RADIO_CLIENT_CSI]=1;
    esp32_mquickjs_wifi_radio_lease_t app={0};
    esp32_mquickjs_wifi_radio_configuration_selection_t request={.start_only=true};
    esp32_mquickjs_wifi_radio_lifecycle_t token={0};bool running=false;
    assert(esp32_mquickjs_wifi_radio_begin_start_lifecycle(&app,NULL,NULL,&request,&token,&running)==0);
    assert(running && !token.identity && app.acquired && app.identity==20 && app.generation==7);
    assert(s_radio.leases[1].identity==20 && s_radio.clients[app.client]==1 && request.storage==WIFI_STORAGE_FLASH);
    assert(s_radio.leases[0].identity==9 && s_radio.next_lease_identity==21);
    assert(esp32_mquickjs_wifi_radio_begin_start_lifecycle(&app,NULL,NULL,&request,&token,&running)==0);
    assert(app.identity==20 && s_radio.next_lease_identity==21 && !locked);
}
