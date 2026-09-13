
static bool native_dynamic,native_11b[2],native_coex;
static int esp_wifi_set_dynamic_cs(bool value) {native_dynamic=value;return sdk_step(true);}
static int esp_wifi_config_11b_rate(wifi_interface_t iface,bool value) {assert(iface==WIFI_IF_STA || iface==WIFI_IF_AP);native_11b[iface]=value;return sdk_step(true);}
#if CONFIG_ESP_COEX_POWER_MANAGEMENT
static int esp_wifi_coex_pwr_configure(bool value) {native_coex=value;return sdk_step(true);}
#endif
