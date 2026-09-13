
int main(void) {
    int result=99;
    band=WIFI_BAND_2G;assert(hostapd_wps_rf_band_cb(NULL)==WPS_RF_24GHZ);
    band=WIFI_BAND_5G;assert(hostapd_wps_rf_band_cb(NULL)==(CONFIG_SOC_WIFI_SUPPORT_5G ? WPS_RF_50GHZ : 0));
    band=WIFI_BAND_2G;assert(hostapd_wps_rf_band_cb(NULL)==WPS_RF_24GHZ);
    sdk_error=77;assert(esp32qjs_wps_ap_rf_band_read(&result)==77 && !result);
    assert(hostapd_wps_rf_band_cb(NULL)==0);
    sdk_error=0;band=99;result=99;
    assert(esp32qjs_wps_ap_rf_band_read(&result)==ESP_ERR_INVALID_RESPONSE && !result);
    assert(hostapd_wps_rf_band_cb(NULL)==0);
    return 0;
}
