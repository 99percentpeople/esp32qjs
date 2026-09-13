void esp32_mquickjs_wifi_raw_tx_pump_wake(void) {}
esp_err_t esp32_mquickjs_wifi_raw_tx_pump_init(void) {return ESP_OK;}

#define esp32_mquickjs_memory_wireless_alloc(owner,size,kind,role) heap_caps_malloc(size,MALLOC_CAP_8BIT)
#define esp32_mquickjs_memory_wireless_calloc(owner,count,size,kind,role) heap_caps_calloc(count,size,MALLOC_CAP_8BIT)
typedef esp32_mquickjs_wifi_raw_tx_token_t token_t;
typedef esp32_mquickjs_wifi_raw_tx_broker_status_t status_t;
typedef esp32_mquickjs_wifi_raw_tx_validation_policy_t policy_t;
static void (*driver_callback)(const esp_80211_tx_info_t *);
static unsigned allocations, frees, sends, registrations, unregisters;
static bool fail_alloc, synchronous, start_on_unregister;
static esp_err_t register_error, unregister_error, send_error;
static const uint8_t *driver_bytes;
static token_t *sending_token;
static esp_80211_tx_info_t callback_info;
static uint8_t destination[6], source_address[6];
static pthread_mutex_t pause_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t changed = PTHREAD_COND_INITIALIZER;
static bool paused, proceed;
static _Thread_local bool pause_clock;
static pthread_t callback_task;
static void *heap_caps_malloc(size_t size, unsigned caps) {
    assert(!critical_depth && caps==MALLOC_CAP_8BIT);
    if(fail_alloc)return NULL;
    void *p=malloc(size);assert(p);++allocations;return p;
}
static void heap_caps_free(void *p) {
    assert(!critical_depth && p && allocations>frees);++frees;free(p);
}
static int64_t esp_timer_get_time(void) {
    assert(!critical_depth);
    if(pause_clock) {
        assert(!pthread_mutex_lock(&pause_lock));paused=true;
        assert(!pthread_cond_broadcast(&changed));
        while(!proceed)assert(!pthread_cond_wait(&changed,&pause_lock));
        assert(!pthread_mutex_unlock(&pause_lock));
    }
    return 123456789;
}
static void *callback_thread(void *unused) {
    (void)unused;pause_clock=true;driver_callback(&callback_info);return NULL;
}
static void start_callback(void) {
    paused=proceed=false;
    assert(!pthread_create(&callback_task,NULL,callback_thread,NULL));
    assert(!pthread_mutex_lock(&pause_lock));
    while(!paused)assert(!pthread_cond_wait(&changed,&pause_lock));
    assert(!pthread_mutex_unlock(&pause_lock));
}
static void finish_callback(void) {
    assert(!pthread_mutex_lock(&pause_lock));proceed=true;
    assert(!pthread_cond_broadcast(&changed));assert(!pthread_mutex_unlock(&pause_lock));
    assert(!pthread_join(callback_task,NULL));
}
static esp_err_t esp_wifi_register_80211_tx_cb(void (*callback)(const esp_80211_tx_info_t *)) {
    assert(!critical_depth);
    if(callback) {++registrations;driver_callback=callback;return register_error;}
    ++unregisters;
    /* A callback already entered may outlive successful unregister. */
    if(start_on_unregister) {start_on_unregister=false;start_callback();}
    return unregister_error;
}
static esp_err_t esp_wifi_80211_tx(wifi_interface_t interface,const void *bytes,int length,bool sequence) {
    assert(!critical_depth && interface==WIFI_IF_STA && length==24 && sequence);
    ++sends;driver_bytes=bytes;
    assert(sending_token && sending_token->identity);
    status_t status;esp32_mquickjs_wifi_raw_tx_broker_status(&status);
    assert(status.operation_active && !status.submit_returned && !status.driver_accepted);
    assert(!esp32_mquickjs_wifi_raw_tx_broker_retire(sending_token));
    assert(!esp32_mquickjs_wifi_raw_tx_broker_reset_after_deinit(sending_token->generation));
    if(synchronous) {
        driver_callback(&callback_info);
        esp32_mquickjs_wifi_raw_tx_broker_status(&status);
        assert(status.driver_completed && !status.submit_returned);
        assert(!esp32_mquickjs_wifi_raw_tx_broker_retire(sending_token));
    }
    return send_error;
}
