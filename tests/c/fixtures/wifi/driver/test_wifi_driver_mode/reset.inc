
static void mode_reset(void) {
    storage_reset();s_radio.effective_mode=native_mode=WIFI_MODE_NULL;
    corrupt_mode_readback=corrupt_mode_rollback=false;mode_hook=NULL;
}
