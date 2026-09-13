
static int esp_wifi_set_inactive_time(wifi_interface_t iface,uint16_t value) {
    assert(s_radio.started && (native_mode & (1U<<iface)));
    ++inactive_writes[iface];inactive[iface]=value;
    if(s_radio.storage==WIFI_STORAGE_FLASH)++inactive_nvs_writes;
    return sdk_step(true);
}
