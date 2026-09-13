
static esp32_mquickjs_wifi_interval_state_t s_interval;
static uint16_t native_interval,native_window;
static unsigned interval_calls,window_calls;
static int interval_error,window_error;
static int esp_wifi_connectionless_module_set_wake_interval(uint16_t value) {
    assert(locks==1 && !critical && !helper_locks);
    ++interval_calls;native_interval=value;return interval_error;
}
typedef struct {
    esp32_mquickjs_wifi_radio_lease_t radio_lease;
    esp32_mquickjs_wifi_interval_token_t interval_token;
    bool power_save_fault,power_save_cleanup_required,now_initialized;
    uint8_t close_power_phase;
} espnow_session_t;
static int esp_now_set_wake_window(uint16_t value) {
    assert(!locks && !critical && !helper_locks);
    ++window_calls;native_window=value;return window_error;
}
