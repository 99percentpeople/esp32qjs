
#if CONFIG_ESP_WIFI_NAN_SYNC_ENABLE
static esp_err_t native_pool_error;
static unsigned query_reads;
esp_err_t esp32_mquickjs_wifi_radio_nan_query(const esp32_mquickjs_wifi_radio_operation_t *token,
    const esp32_mquickjs_wifi_nan_query_t *query, esp32_mquickjs_wifi_nan_query_result_t *out){
    assert(!lock_depth&&token->identity&&query);++query_reads;
    *out=(esp32_mquickjs_wifi_nan_query_result_t){.service_id=7};return ESP_OK;
}
void esp32_mquickjs_wifi_nan_tx_status(esp32_mquickjs_wifi_nan_tx_status_t *out){
    *out=(esp32_mquickjs_wifi_nan_tx_status_t){.error=native_pool_error};
}
#endif
static unsigned allocations, allocation_calls, fail_allocation, begins, closes;
static int64_t clock_us;
static int64_t start_advance_us;
static bool queue_full, pause_begin, begin_entered, resume_begin;
static int begin_error, close_error;
static bool native_started;
static pthread_mutex_t gate = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t condition = PTHREAD_COND_INITIALIZER;
static void (*queued)(void *);
static void *queued_arg;
static esp32_mquickjs_wifi_nan_sdk_observer_fn native_observer;
static void *native_opaque;
static int64_t esp_timer_get_time(void) {return clock_us;}
static void *esp32_mquickjs_memory_wireless_calloc(const char *owner, size_t count, size_t size, int cls, int role) {
    assert(!lock_depth && !strcmp(owner, "wifi.nan") && !cls && role==1);
    if (++allocation_calls==fail_allocation) return NULL;
    void *p=calloc(count,size); if(p)++allocations; return p;
}
static void esp32_mquickjs_memory_payload_free(void *p) {assert(!lock_depth&&p&&allocations);--allocations;free(p);}
static void esp32_mquickjs_wireless_secure_zero(void *p,size_t n) {memset(p,0,n);}
static bool esp32_mquickjs_submit_background_worker(void (*fn)(void *),void *p) {
    assert(!lock_depth); if(queue_full)return false;assert(!queued);queued=fn;queued_arg=p;return true;
}
esp_err_t esp32_mquickjs_wifi_radio_nan_begin(const wifi_nan_sync_config_t *config,
    esp32_mquickjs_wifi_nan_sdk_observer_fn observer,void *opaque,
    esp32_mquickjs_wifi_radio_operation_t *token,esp32_mquickjs_wifi_nan_radio_status_t *status) {
    assert(!lock_depth&&config->op_channel==6);++begins;
    native_observer=observer;native_opaque=opaque;
    if(begin_error){status->error=begin_error;status->stage="nan-admission";return begin_error;}
    native_started=true;
    *token=(esp32_mquickjs_wifi_radio_operation_t){.generation=7,.identity=begins,.lease_identity=3};
    *status=(esp32_mquickjs_wifi_nan_radio_status_t){.operation=*token,.start_attempted=true,.start_accepted=true,.ready=true,.reserved_bytes=32};
    esp32_mquickjs_wifi_nan_sdk_notice_t notice={.kind=ESP32_MQUICKJS_NAN_SDK_STARTED};
    observer(begins,&notice,opaque);
    pthread_mutex_lock(&gate);
    if(pause_begin){begin_entered=true;pthread_cond_broadcast(&condition);while(!resume_begin)pthread_cond_wait(&condition,&gate);}
    pthread_mutex_unlock(&gate);
    clock_us += start_advance_us;
    return ESP_OK;
}
esp_err_t esp32_mquickjs_wifi_radio_nan_usd_begin(esp32_mquickjs_wifi_nan_sdk_observer_fn observer,void *opaque,
    esp32_mquickjs_wifi_radio_operation_t *token,esp32_mquickjs_wifi_nan_radio_status_t *status){
#if CONFIG_ESP_WIFI_NAN_USD_ENABLE
    wifi_nan_sync_config_t config={.op_channel=6};
    esp_err_t error=esp32_mquickjs_wifi_radio_nan_begin(&config,observer,opaque,token,status);
    status->usd=true;return error;
#else
    (void)observer;(void)opaque;(void)token;(void)status;return ESP_ERR_NOT_SUPPORTED;
#endif
}
esp_err_t esp32_mquickjs_wifi_radio_nan_poll(const esp32_mquickjs_wifi_radio_operation_t *token,
    esp32_mquickjs_wifi_nan_radio_status_t *status){(void)token;(void)status;
#if CONFIG_ESP_WIFI_NAN_USD_ENABLE
    return ESP_OK;
#else
    return ESP_ERR_NOT_SUPPORTED;
#endif
}
esp_err_t esp32_mquickjs_wifi_radio_nan_close(esp32_mquickjs_wifi_radio_operation_t *token,
    esp32_mquickjs_wifi_nan_radio_status_t *status) {
    assert(!lock_depth&&token->identity&&allocations);++closes;
    status->ready=false;status->closing=true;
    status->cleanup_error=close_error;status->cleanup_stage=close_error?"nan-stop-events":NULL;
    if(!close_error){native_started=false;memset(token,0,sizeof(*token));status->operation=*token;status->reserved_bytes=0;}
    return close_error;
}
static void *run_queued(void *unused) {
    (void)unused;assert(queued);void (*fn)(void *)=queued;void *arg=queued_arg;queued=NULL;queued_arg=NULL;fn(arg);return NULL;
}
