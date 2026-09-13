
int main(void) {
    setup();wifi_radio_operation_lock();
    assert(wifi_radio_restart_configs_capture_locked(&token,WIFI_MODE_STA)==ESP_OK);
    uint8_t saved[2][6];memcpy(saved,native_mac,sizeof(saved));
    new_driver();
    assert(wifi_radio_restart_configs_replay_locked(&token)==ESP_OK);
    assert(!s_radio.started && !native_running && !native_starts);
    assert(!memcmp(saved[0],native_mac[0],6));
    if(CONFIG_ESP_WIFI_SOFTAP_SUPPORT)assert(!memcmp(saved[1],native_mac[1],6));
    assert(!nvs_writes);
    wipe();wifi_radio_operation_unlock();return 0;
}
