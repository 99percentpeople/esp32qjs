
static wifi_config_t configs[2];
static bool corrupt_pmf,corrupt_other;
static void (*pmf_hook)(void);
static int esp_wifi_get_config(wifi_interface_t iface,wifi_config_t *out) {
    *out=configs[iface];return sdk_step(false);
}
static int esp_wifi_disable_pmf_config(wifi_interface_t iface) {
    wifi_pmf_config_t *pmf=iface==WIFI_IF_STA ? &configs[iface].sta.pmf_cfg : &configs[iface].ap.pmf_cfg;
    pmf->capable=false;int err=sdk_step(true); /* A prefix can change even when the SDK reports failure. */
    if(err==ESP_OK && !corrupt_pmf)pmf->required=false;
    if(corrupt_other)configs[iface].sta.ssid[0]^=1;
    if(pmf_hook)pmf_hook();return err;
}
