
static int offset_error,mode_error;static unsigned offset_calls,mode_calls;
static int16_t native_offset;static bool mode_mismatch;
static int esp_wifi_get_mode(wifi_mode_t *mode) {
    assert(locks && !critical);++mode_calls;
    *mode=mode_mismatch?WIFI_MODE_STA:s_radio.effective_mode;return mode_error;
}
static int esp_wifi_ftm_resp_set_offset(int16_t value) {
    assert(locks && !critical && !s_radio.started && s_ftm_offset.uncertain && !s_ftm_offset.known);
    ++offset_calls;native_offset=value;return offset_error;
}
