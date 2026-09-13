
static void (*worker)(void *);
static void *worker_arg;
static bool worker_full;
static unsigned reads;
static int read_error;
static esp32_mquickjs_wifi_twt_broadcast_snapshot_t discovered;
esp_err_t esp32_mquickjs_wifi_twt_sdk_broadcast_snapshot(esp32_mquickjs_wifi_twt_broadcast_snapshot_t *out) {
    assert(locks && !critical);++reads;if(read_error)return read_error;*out=discovered;return 0;
}
static bool esp32_mquickjs_submit_background_worker(void (*fn)(void *),void *arg) {
    assert(!critical);if(worker_full || worker)return false;worker=fn;worker_arg=arg;return true;
}
static void work(void) {
    assert(worker);void (*fn)(void *)=worker;void *arg=worker_arg;worker=NULL;worker_arg=NULL;fn(arg);
}
static const char *esp_err_to_name(int error) {(void)error;return "injected";}
