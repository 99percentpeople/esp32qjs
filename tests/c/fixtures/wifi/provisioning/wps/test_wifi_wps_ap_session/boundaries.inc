
static unsigned locks,allocs,frees,starts,finishes,prepares,closes,pin_copies,pin_commits,copies,commits;
static unsigned helper_identity,next_helper=1,helper_drains,helper_releases;
static bool alloc_fail,queue_fail,partial_start,capture_ready,hold_close,hold_helper,radio_active,helper_ready;
static int start_error,poll_error,finish_error,reserve_error;
static int64_t now=100;
static size_t allocation_size;
static void (*scheduled)(void *),(*during_transfer)(void);
static void *scheduled_arg;
static esp32_mquickjs_wifi_wps_ap_radio_status_t radio_state;
#define portENTER_CRITICAL(p) do{(void)(p);assert(!locks);locks=1;}while(0)
#define portEXIT_CRITICAL(p) do{(void)(p);assert(locks);locks=0;}while(0)
static void *heap_caps_calloc(size_t n,size_t size,int caps) {
    assert(!locks && caps==1);if(alloc_fail)return NULL;allocs++;allocation_size=n*size;return calloc(n,size);
}
static void heap_caps_free(void *p) {
    assert(!locks && p);for(size_t i=0;i<allocation_size;i++)assert(!((uint8_t*)p)[i]);frees++;free(p);
}
static void esp32_mquickjs_wireless_secure_zero(void *p,size_t n) {volatile uint8_t *v=p;while(n--)*v++=0;}
static int64_t esp_timer_get_time(void) {return now;}
static bool esp32_mquickjs_submit_background_worker(void (*fn)(void *),void *arg) {
    assert(!locks && !scheduled);if(queue_fail)return false;scheduled=fn;scheduled_arg=arg;return true;
}
bool esp32_mquickjs_wifi_wps_config_valid(const esp_wps_config_t *c) {return c && c->wps_type==WPS_TYPE_PIN;}
esp_err_t esp32_mquickjs_wifi_wps_ap_helper_reserve(uint32_t *identity,esp32_mquickjs_wifi_radio_lease_t owners[3]) {
    assert(!locks && !*identity && !helper_identity);
    if(reserve_error)return reserve_error;*identity=helper_identity=next_helper++;
    for(unsigned i=0;i<3;i++)owners[i]=(esp32_mquickjs_wifi_radio_lease_t){.identity=i+1,.generation=5,.client=i,.acquired=true};
    return 0;
}
esp_err_t esp32_mquickjs_wifi_wps_ap_helper_release(uint32_t *identity) {
    assert(!locks && !radio_active && *identity==helper_identity);
    if(hold_helper)return ESP_ERR_NOT_FINISHED;
    helper_releases++;helper_identity=*identity=0;return 0;
}
esp_err_t esp32_mquickjs_wifi_radio_wps_ap_begin(const esp32_mquickjs_wifi_radio_lease_t owners[3],
    const esp_wps_config_t *config,esp32_mquickjs_wifi_radio_operation_t *token,esp32_mquickjs_wifi_wps_ap_radio_status_t *status) {
    assert(!locks && helper_identity && !token->identity && owners[1].identity==2 && config->wps_type==WPS_TYPE_PIN);
    starts++;if(!start_error || partial_start){*token=(esp32_mquickjs_wifi_radio_operation_t){.identity=21,.generation=5};radio_active=true;}
    radio_state=(esp32_mquickjs_wifi_wps_ap_radio_status_t){.operation=*token};
    radio_state.worker.native.pin_available=true;*status=radio_state;return start_error;
}
esp_err_t esp32_mquickjs_wifi_radio_wps_ap_status(const esp32_mquickjs_wifi_radio_operation_t *token,
    esp32_mquickjs_wifi_wps_ap_radio_status_t *status) {
    assert(!locks && token->identity==21 && radio_active);radio_state.worker.native.terminal=capture_ready;
    *status=radio_state;status->error=poll_error;return 0;
}
esp_err_t esp32_mquickjs_wifi_radio_wps_ap_pin(const esp32_mquickjs_wifi_radio_operation_t *token,uint8_t pin[8],bool commit) {
    assert(!locks && token->identity==21 && radio_active);
    if(commit){pin_commits++;radio_state.worker.native.pin_available=false;}else{pin_copies++;memcpy(pin,"12345670",8);}return 0;
}
esp_err_t esp32_mquickjs_wifi_radio_wps_ap_finish_capture(const esp32_mquickjs_wifi_radio_operation_t *token,
    esp32_mquickjs_wifi_wps_ap_radio_status_t *status) {
    assert(!locks && token->identity==21 && capture_ready);finishes++;
    if(finish_error)return finish_error;radio_state.worker.capture_retired=true;memcpy(radio_state.worker.native.peer,"ABCDEF",6);*status=radio_state;return 0;
}
esp_err_t esp32_mquickjs_wifi_radio_wps_ap_close(esp32_mquickjs_wifi_radio_operation_t *token,
    esp32_mquickjs_wifi_wps_ap_radio_status_t *status) {
    assert(!locks && token->identity==21 && helper_identity && radio_active);
    closes++;if(hold_close)return ESP_ERR_NOT_FINISHED;
    radio_state.worker.retired=true;*status=radio_state;token->identity=0;radio_active=false;return 0;
}
