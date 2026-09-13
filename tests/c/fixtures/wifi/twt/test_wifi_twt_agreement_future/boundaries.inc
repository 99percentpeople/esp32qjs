
static int64_t now_us;
static void (*worker)(void *);
static void *worker_arg;
static int64_t esp_timer_get_time(void) {return now_us;}
static bool esp32_mquickjs_submit_background_worker(void (*fn)(void *),void *arg) {
    if(worker)return false;worker=fn;worker_arg=arg;return true;
}
static void work(void) {
    assert(worker);void (*fn)(void *)=worker;void *arg=worker_arg;worker=NULL;worker_arg=NULL;fn(arg);
}
