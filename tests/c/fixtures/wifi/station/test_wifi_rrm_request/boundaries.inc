
#define ESP_FAIL -9
#define ESP_ERR_WIFI_NOT_STARTED -10
#define ESP_ERR_TIMEOUT -11
#define ESP_ERR_INVALID_RESPONSE -12
#define ESP_ERR_INVALID_SIZE -13
#define MALLOC_CAP_8BIT 1
typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
static unsigned request_lock;
#define portENTER_CRITICAL(p) do{(void)(p);assert(!request_lock);request_lock=1;}while(0)
#define portEXIT_CRITICAL(p) do{(void)(p);assert(request_lock);request_lock=0;}while(0)
static int64_t now;
static int64_t esp_timer_get_time(void) {assert(!request_lock);return now;}
static unsigned alloc_calls, fail_alloc, live_allocs;
static void *heap_caps_calloc(size_t n,size_t bytes,unsigned flags) {
    (void)flags;assert(!request_lock);if(++alloc_calls==fail_alloc)return NULL;
    void *p=calloc(n,bytes);assert(p);live_allocs++;return p;
}
static void *heap_caps_malloc(size_t bytes,unsigned flags) {return heap_caps_calloc(1,bytes,flags);}
static void heap_caps_free(void *p) {assert(!request_lock && live_allocs);live_allocs--;free(p);}
static bool queue_full;
static void (*queued)(void *);
static void *queued_arg;
static bool esp32_mquickjs_submit_background_worker(void (*fn)(void *),void *arg) {
    assert(!request_lock && !queued);if(queue_full)return false;queued=fn;queued_arg=arg;return true;
}
static void run_worker(void) {assert(queued);void(*fn)(void*)=queued;queued=NULL;now+=200000;fn(queued_arg);}
static esp32_mquickjs_wifi_radio_lease_t application,station,access_point;
static bool rm_enabled=true;
static int config_error;
static esp_err_t esp_wifi_get_config(wifi_interface_t interface,wifi_config_t *config) {
    assert(locks && !critical && !request_lock && interface==WIFI_IF_STA);
    memset(config,0xa5,sizeof(*config));config->sta.rm_enabled=rm_enabled;return config_error;
}
static esp32_mquickjs_wifi_rrm_callback_t pending_callback;
static void *pending_identity;
static unsigned submissions, cancellations, probes;
static bool dispatch_fail, early_report;
static int submit_code,cancel_code;
static uint8_t payload[32]={1,52,13,2,0,0,0,0,1};
static unsigned observation_attempts,observations;
static bool observation_queue_full;
void esp32qjs_rrm_publish_observation(const uint8_t *report,size_t length) {
    assert(!request_lock && !locks && !critical && ((report!=NULL)==(length!=0)));
    observation_attempts++;if(!observation_queue_full)observations++;
}
esp_err_t esp32_mquickjs_wifi_rrm_sdk_command(esp32_mquickjs_wifi_rrm_command_t command,
    esp32_mquickjs_wifi_rrm_callback_t callback,void *id,esp32_mquickjs_wifi_rrm_sdk_result_t *r) {
    assert(locks && !critical && !request_lock && callback && id);
    *r=(esp32_mquickjs_wifi_rrm_sdk_result_t){.ownership=-1};
    if(dispatch_fail)return ESP_FAIL;
    r->entered=true;
    if(command==ESP32_MQUICKJS_WIFI_RRM_SUBMIT) {
        assert(!pending_callback);submissions++;r->tx_attempted=true;r->code=submit_code;
        if(!submit_code){pending_callback=callback;pending_identity=id;}
        if(early_report && pending_callback){callback(id,payload,16);pending_callback=NULL;pending_identity=NULL;}
    } else if(command==ESP32_MQUICKJS_WIFI_RRM_CANCEL) {
        cancellations++;r->code=cancel_code;
        if(!cancel_code && pending_callback==callback && pending_identity==id){pending_callback=NULL;pending_identity=NULL;}
    } else probes++;
    r->ownership=!pending_callback ? 0 : pending_callback==callback && pending_identity==id ? 1 : -16;
    if(command==ESP32_MQUICKJS_WIFI_RRM_QUERY)r->code=r->ownership;
    return ESP_OK;
}
static void deliver(const uint8_t *report,size_t bytes) {
    assert(pending_callback && !locks && !request_lock);
    pending_callback(pending_identity,report,bytes);
    pending_callback=NULL;pending_identity=NULL;
}
