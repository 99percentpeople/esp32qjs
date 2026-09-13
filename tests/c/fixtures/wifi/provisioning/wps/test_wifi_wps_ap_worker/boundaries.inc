
static unsigned allocations,start_calls,stop_calls,retire_calls,release_calls,copy_calls,commit_calls;
static unsigned creates,starts,stops,deletes;
static int ipc_mode,create_error,start_error,stop_error,delete_error,release_error;
static void *allocation;
static size_t allocation_size;
static esp_timer_create_args_t timer_args;
static bool timer_exists,timer_returned,timer_deleted;
static esp32_mquickjs_wifi_wps_ap_result_status_t native;
static uint32_t callback_revision;
static void esp32_mquickjs_wireless_secure_zero(void *p,size_t n) {memset(p,0,n);}
static void *heap_caps_calloc(size_t count,size_t size,unsigned caps) {
    assert(count==1 && caps==3 && !allocations);
    allocation=calloc(count,size);assert(allocation);allocation_size=size;allocations++;return allocation;
}
static void heap_caps_free(void *p) {
    assert(p==allocation && allocations==1);
    for(size_t i=0;i<allocation_size;i++)assert(!((uint8_t *)p)[i]);
    free(p);allocation=NULL;allocations=0;
}
static esp_err_t esp_timer_create(const esp_timer_create_args_t *args,esp_timer_handle_t *out) {
    creates++;assert(args->dispatch_method==ESP_TIMER_TASK && !timer_exists);
    if(create_error)return create_error;timer_args=*args;timer_exists=true;*out=&timer_args;return ESP_OK;
}
static esp_err_t esp_timer_start_once(esp_timer_handle_t timer,uint32_t us) {
    starts++;assert(timer==&timer_args && timer_exists && us==1);
    if(start_error)return start_error;
    timer_returned=false;timer_deleted=false;return ESP_OK;
}
static esp_err_t esp_timer_stop_blocking(esp_timer_handle_t timer,uint32_t timeout) {
    stops++;assert(timer==&timer_args && timeout==1 && timer_returned);return stop_error;
}
static esp_err_t esp_timer_delete(esp_timer_handle_t timer) {
    deletes++;assert(timer==&timer_args && timer_exists);
    if(delete_error)return delete_error;timer_exists=false;timer_deleted=true;return ESP_OK;
}
esp_err_t esp32qjs_wps_ap_native_begin(const esp_wps_config_t *config,uint32_t *identity) {
    assert(config->wps_type==WPS_TYPE_PIN && !*identity);*identity=41;
    native=(esp32_mquickjs_wifi_wps_ap_result_status_t){.identity=41,.enabled=true};return ESP_OK;
}
esp_err_t esp32qjs_wps_ap_native_start(uint32_t identity) {assert(identity==41);start_calls++;return ESP_OK;}
esp_err_t esp32qjs_wps_ap_result_status(uint32_t identity,esp32_mquickjs_wifi_wps_ap_result_status_t *out) {
    assert(identity==41);*out=native;return ESP_OK;
}
esp_err_t esp32qjs_wps_ap_native_stop(uint32_t identity) {
    assert(identity==41);stop_calls++;native.closing=true;native.close_prepared=true;
    return ESP_OK;
}
esp_err_t esp32qjs_wps_ap_native_retire(uint32_t identity) {
    assert(identity==41 && native.close_prepared && timer_returned && timer_deleted);
    retire_calls++;native.sdk_state_retired=true;return ESP_OK;
}
esp_err_t esp32qjs_wps_ap_native_checkpoint(uint32_t identity,uint32_t *revision) {
    assert(identity==41 && native.sdk_state_retired);*revision=callback_revision;return ESP_OK;
}
esp_err_t esp32qjs_wps_ap_native_release(uint32_t identity,uint32_t revision) {
    release_calls++;assert(identity==41 && timer_deleted);
    if(release_error)return release_error;
    assert(revision==callback_revision);return ESP_OK;
}
esp_err_t esp32qjs_wps_ap_result_pin_copy(uint32_t identity,uint8_t pin[8]) {
    assert(identity==41);memcpy(pin,"12345670",8);copy_calls++;return ESP_OK;
}
esp_err_t esp32qjs_wps_ap_result_pin_commit(uint32_t identity) {assert(identity==41);commit_calls++;return ESP_OK;}
