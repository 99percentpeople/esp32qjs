
#define ESP_ERR_TIMEOUT -33
static unsigned esp32_mquickjs_wifi_wait_remaining(unsigned fallback) { return fallback; }
static bool s_wifi_configuration_disconnect_pending;
static struct { unsigned identity; } s_wifi_lifecycle;
typedef int esp32_mquickjs_native_wait_t;
static int wait_depth,wait_begins,wait_ends;
static bool cooperate_ok=true;
static esp32_mquickjs_runtime_t *esp32_mquickjs_get_active_runtime(void) { return (void *)1; }
static void esp32_mquickjs_native_wait_begin(void *runtime,int *wait) {
    assert(runtime && !wait_depth);(void)wait;wait_depth=1;wait_begins++;
}
static void esp32_mquickjs_native_wait_end(void *runtime,int *wait) {
    assert(runtime && wait_depth);(void)wait;wait_depth=0;wait_ends++;
}
static bool esp32_mquickjs_cooperate(void *runtime) { assert(runtime && wait_depth);return cooperate_ok; }
