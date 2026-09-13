
static unsigned locks,allocs,frees,starts,finishes,prepares,closes,pin_copies,pin_commits,copies,commits;
static unsigned helper_identity,next_helper=1,helper_drains,helper_releases;
static bool alloc_fail,queue_fail,partial_start,capture_ready,hold_close,hold_helper,radio_active,helper_ready;
static int start_error,poll_error,finish_error,reserve_error;
static int64_t now=100;
static size_t allocation_size;
static void (*scheduled)(void *),(*during_transfer)(void);
static void *scheduled_arg;
static esp32_mquickjs_wifi_wps_radio_status_t radio_state;
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
esp_err_t esp32_mquickjs_wifi_wps_station_reserve(uint32_t *identity,esp32_mquickjs_wifi_radio_lease_t owners[3]) {
    assert(!locks && !*identity && !helper_identity);
    if(reserve_error)return reserve_error;*identity=helper_identity=next_helper++;
    for(unsigned i=0;i<3;i++)owners[i]=(esp32_mquickjs_wifi_radio_lease_t){.identity=i+1,.generation=5,.client=i,.acquired=true};
    return 0;
}
esp_err_t esp32_mquickjs_wifi_wps_station_drain(uint32_t identity) {
    assert(!locks && identity==helper_identity && identity);helper_drains++;
    if(radio_active)assert(radio_state.worker.capture_retired || radio_state.worker.retired);
    if(hold_helper)return ESP_ERR_NOT_FINISHED;helper_ready=true;return 0;
}
esp_err_t esp32_mquickjs_wifi_wps_station_release(uint32_t *identity) {
    assert(!locks && !radio_active);int error=esp32_mquickjs_wifi_wps_station_drain(*identity);
    if(error)return error;helper_releases++;helper_identity=*identity=0;return 0;
}
esp_err_t esp32_mquickjs_wifi_radio_wps_begin(const esp32_mquickjs_wifi_radio_lease_t owners[3],bool allow,
    const esp_wps_config_t *config,esp32_mquickjs_wifi_radio_operation_t *token,esp32_mquickjs_wifi_wps_radio_status_t *status) {
    assert(!locks && helper_identity && !token->identity && owners[1].identity==2 && config->wps_type==WPS_TYPE_PIN);
    (void)allow;starts++;if(!start_error || partial_start){*token=(esp32_mquickjs_wifi_radio_operation_t){.identity=21,.generation=5};radio_active=true;}
    radio_state=(esp32_mquickjs_wifi_wps_radio_status_t){.operation=*token};
    radio_state.worker.native.pin_available=true;*status=radio_state;return start_error;
}
esp_err_t esp32_mquickjs_wifi_radio_wps_status(const esp32_mquickjs_wifi_radio_operation_t *token,
    esp32_mquickjs_wifi_wps_radio_status_t *status) {
    assert(!locks && token->identity==21 && radio_active);radio_state.worker.native.terminal_seen=capture_ready;
    *status=radio_state;status->error=poll_error;return 0;
}
esp_err_t esp32_mquickjs_wifi_radio_wps_pin(const esp32_mquickjs_wifi_radio_operation_t *token,uint8_t pin[8],bool commit) {
    assert(!locks && token->identity==21 && radio_active);
    if(commit){pin_commits++;radio_state.worker.native.pin_available=false;}else{pin_copies++;memcpy(pin,"12345670",8);}return 0;
}
esp_err_t esp32_mquickjs_wifi_radio_wps_finish_capture(const esp32_mquickjs_wifi_radio_operation_t *token,
    esp32_mquickjs_wifi_wps_radio_status_t *status) {
    assert(!locks && token->identity==21 && capture_ready);finishes++;
    if(finish_error)return finish_error;radio_state.worker.capture_retired=true;*status=radio_state;return 0;
}
esp_err_t esp32_mquickjs_wifi_radio_wps_credentials(const esp32_mquickjs_wifi_radio_operation_t *token,
    esp32_mquickjs_wifi_wps_credentials_t *out,bool commit) {
    assert(!locks && token->identity==21 && radio_state.worker.capture_retired && helper_ready);
    if(commit)commits++;else {copies++;memset(out,0,sizeof(*out));out->count=1;memcpy(out->entries[0].ssid,"network",7);out->entries[0].ssid_length=7;}
    if(during_transfer){void (*fn)(void)=during_transfer;during_transfer=NULL;fn();}return 0;
}
esp_err_t esp32_mquickjs_wifi_radio_wps_prepare_close(const esp32_mquickjs_wifi_radio_operation_t *token,
    esp32_mquickjs_wifi_wps_radio_status_t *status) {
    assert(!locks && token->identity==21 && radio_active);prepares++;
    if(hold_close)return ESP_ERR_NOT_FINISHED;radio_state.worker.retired=true;helper_ready=false;*status=radio_state;return 0;
}
esp_err_t esp32_mquickjs_wifi_radio_wps_close(esp32_mquickjs_wifi_radio_operation_t *token,
    esp32_mquickjs_wifi_wps_radio_status_t *status) {
    assert(!locks && token->identity==21 && radio_state.worker.retired && helper_ready && helper_identity);
    closes++;*status=radio_state;token->identity=0;radio_active=false;return 0;
}
