
static void storage_reset(void) {
    reset();s_radio.started=s_radio.stop_required=false;
    s_radio.driver_state=ESP32_MQUICKJS_WIFI_RADIO_STOPPED;
    s_radio.storage=native_storage=WIFI_STORAGE_RAM;
    memset(s_radio.leases,0,sizeof(s_radio.leases));s_tx_rate_lease.restore_pending=false;
    storage_hook=NULL;
}
