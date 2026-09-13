
static void pmf_reset(void) {
    storage_reset();memset(configs,0,sizeof(configs));
    configs[0].sta.threshold.authmode=WIFI_AUTH_WPA2_PSK;configs[0].sta.disable_wpa3_compatible_mode=true;
    configs[1].ap.authmode=WIFI_AUTH_WPA2_PSK;
    configs[0].sta.pmf_cfg=(wifi_pmf_config_t){.capable=true,.required=true};
    configs[1].ap.pmf_cfg=configs[0].sta.pmf_cfg;
    memcpy(configs[0].sta.password,"station-secret",14);memcpy(configs[1].ap.password,"ap-secret",9);
    corrupt_pmf=corrupt_other=false;pmf_hook=NULL;
}
