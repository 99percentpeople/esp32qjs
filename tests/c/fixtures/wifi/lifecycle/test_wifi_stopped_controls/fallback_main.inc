
int main(void) {
    stopped();assert(esp32_mquickjs_wifi_radio_set_storage(WIFI_STORAGE_RAM,&result)==ESP_OK);
    assert(!unchanged());admit_without_history();
    stopped();assert(rssi()==ESP_OK && !unchanged());admit_without_history();
    stopped();uint32_t actual=0;
    assert(esp32_mquickjs_wifi_radio_event_mask(true,1,&actual,&result)==ESP_OK && !unchanged());admit_without_history();
    assert(!allocation && !locks && !critical && !helper_locks);return 0;
}
