
static bool native_bss;
static unsigned bitmap_resets;
static int esp_wifi_enable_bsscolor_collision_detection(wifi_interface_t iface,bool value) {
    assert(iface==WIFI_IF_STA && s_radio.started && (s_radio.effective_mode&WIFI_MODE_STA));
    int error=sdk_step(true);
    if(!error){native_bss=value;++bitmap_resets;}
    return error;
}
