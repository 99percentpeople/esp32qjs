
static void band_reset(void) {
    reset();memset(&s_ap_lease,0,sizeof(s_ap_lease));memset(&s_radio.leases[2],0,sizeof(s_radio.leases[2]));
    s_radio.effective_mode=WIFI_MODE_STA;s_radio.storage=WIFI_STORAGE_RAM;
    native_band_mode=WIFI_BAND_MODE_2G_ONLY;native_band=WIFI_BAND_2G;native_channel=6;
    associated=corrupt_band=corrupt_channel=deny_regulatory=false;
}
