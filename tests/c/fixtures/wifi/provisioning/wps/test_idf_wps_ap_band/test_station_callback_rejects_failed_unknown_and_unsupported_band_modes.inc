
int main(void) {
    band_mode=WIFI_BAND_MODE_2G_ONLY;assert(wps_rf_band_cb(NULL)==WPS_RF_24GHZ);
    band_mode=WIFI_BAND_MODE_5G_ONLY;
    assert(wps_rf_band_cb(NULL)==(CONFIG_SOC_WIFI_SUPPORT_5G ? WPS_RF_50GHZ : 0));
    band_mode=WIFI_BAND_MODE_AUTO;
    assert(wps_rf_band_cb(NULL)==(CONFIG_SOC_WIFI_SUPPORT_5G ? (WPS_RF_24GHZ|WPS_RF_50GHZ) : 0));
    sdk_error=77;assert(wps_rf_band_cb(NULL)==0);
    sdk_error=0;band_mode=99;assert(wps_rf_band_cb(NULL)==0);
    assert(reads==5);
    return 0;
}
