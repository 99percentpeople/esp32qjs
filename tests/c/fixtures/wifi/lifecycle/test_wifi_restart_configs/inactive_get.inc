
static wifi_mode_t native_mode;
static unsigned inactive_reads[2],inactive_writes[2],inactive_nvs_writes;
static bool corrupt_inactive;
static int esp_wifi_get_inactive_time(wifi_interface_t iface,uint16_t *value) {
    assert(s_radio.started && (native_mode & (1U<<iface)));
    ++inactive_reads[iface];*value=inactive[iface];
    if(corrupt_inactive && inactive_writes[iface])*value+=1;
    return sdk_step(false);
}
