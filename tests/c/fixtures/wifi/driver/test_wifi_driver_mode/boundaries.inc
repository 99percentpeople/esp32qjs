
static wifi_mode_t native_mode;
static bool corrupt_mode_readback,corrupt_mode_rollback;
static void (*mode_hook)(void);
static int esp_wifi_get_mode(wifi_mode_t *out) {
    *out=native_mode;
    if(corrupt_mode_readback && result.mutation_attempted && !result.rollback_attempted)*out=WIFI_MODE_APSTA;
    if(corrupt_mode_rollback && result.rollback_attempted)*out=WIFI_MODE_APSTA;
    int err=sdk_step(false);if(mode_hook)mode_hook();return err;
}
static int esp_wifi_set_mode(wifi_mode_t mode) {
    assert(!s_radio.started && !s_radio.stop_required);
    native_mode=mode;int err=sdk_step(true); /* Accepted mutation can precede SDK error. */
    if(mode_hook)mode_hook();return err;
}
