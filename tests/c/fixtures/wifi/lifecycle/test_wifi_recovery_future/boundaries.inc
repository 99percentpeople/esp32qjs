
static int64_t clock_us;
static int wait_error;static unsigned wait_begins,wait_ends,last_wait;
static int64_t esp_timer_get_time(void) {return clock_us;}
static int esp32_mquickjs_wifi_wait_begin(uint32_t milliseconds) {
    ++wait_begins;last_wait=milliseconds;return wait_error;
}
static void esp32_mquickjs_wifi_wait_end(void) {++wait_ends;}
