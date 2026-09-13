
int main(void) {
    const wifi_storage_t values[]={WIFI_STORAGE_RAM,WIFI_STORAGE_FLASH};
    for(unsigned i=0;i<2;++i) {
        stopped();esp32_mquickjs_wifi_radio_stop_snapshot_t before=s_stop_snapshot;
        assert(esp32_mquickjs_wifi_radio_set_storage(values[i],&result)==ESP_OK);
        assert(unchanged() && calls==1 && !memcmp(&before,&s_stop_snapshot,sizeof(before)));
        reconstruct(values[i]);
    }
    stopped();fail_at=1;
    assert(esp32_mquickjs_wifi_radio_set_storage(WIFI_STORAGE_RAM,&result)==77);
    assert(!unchanged() && s_stop_snapshot.unchanged && !s_radio.storage_configured);
    unsigned attempts=calls;reject();assert(calls==attempts);
    fail_at=0;assert(esp32_mquickjs_wifi_radio_set_storage(WIFI_STORAGE_RAM,&result)==ESP_OK);
    assert(unchanged() && !s_radio.fault_stage && s_radio.storage_configured);
    reconstruct(WIFI_STORAGE_RAM);
    for(unsigned failure=0;failure<2;++failure) {
        stopped();fail_at=failure ? 1 : 0;
        esp32_mquickjs_wifi_radio_stop_snapshot_t before=s_stop_snapshot;
        assert(rssi()==(failure ? 77 : ESP_OK));
        assert(unchanged() && calls==1 && !memcmp(&before,&s_stop_snapshot,sizeof(before)));
        esp32_mquickjs_wifi_rssi_request_t request=s_rssi_request;
        fail_at=0;reconstruct(WIFI_STORAGE_FLASH);
        assert(!memcmp(&request,&s_rssi_request,sizeof(request))); /* No RSSI replay. */
    }
    stopped();memset(&s_stop_snapshot,0,sizeof(s_stop_snapshot));
    assert(esp32_mquickjs_wifi_radio_set_storage(WIFI_STORAGE_RAM,&result)==ESP_OK);
    assert(rssi()==ESP_OK && !unchanged());admit_without_history(); /* Capture must obtain new runtime observations. */
    stopped();wifi_radio_operation_lock();assert(esp_wifi_set_mode(WIFI_MODE_STA)==ESP_OK);wifi_radio_operation_unlock();
    assert(esp32_mquickjs_wifi_radio_set_storage(WIFI_STORAGE_RAM,&result)==ESP_OK);
    assert(rssi()==ESP_OK && !unchanged());admit_without_history(); /* Does not revive the old history. */
    stopped();++s_radio.event_identity;
    assert(esp32_mquickjs_wifi_radio_set_storage(WIFI_STORAGE_RAM,&result)==ESP_OK && !unchanged());admit_without_history();
    stopped();assert(rssi()==ESP_OK);s_radio.leases[0].identity=9;
    reject(); /* Preserved source evidence never bypasses a new owner. */
    assert(!allocation && !locks && !critical && !helper_locks);return 0;
}
