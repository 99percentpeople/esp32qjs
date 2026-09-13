
int main(void) {
    sdk_error=77;band_mode=WIFI_BAND_MODE_5G_ONLY;
    assert(wps_rf_band_cb(NULL)==WPS_RF_24GHZ && reads==1);
    sdk_error=0;band_mode=99;
    assert(wps_rf_band_cb(NULL)==WPS_RF_24GHZ);
    return 0;
}
