
struct esp32_mquickjs_wifi_wps_worker {esp32_mquickjs_wifi_wps_worker_status_t status;bool prepared;};
static struct esp32_mquickjs_wifi_wps_worker worker;
static wifi_config_t original,actual;
static wifi_storage_t storage;
static uint8_t channel;
static wifi_second_chan_t secondary;
static int prepare_error,start_error,close_error,storage_error,config_error,channel_error,fence_error;
static int peer_error=ESP_ERR_WIFI_NOT_CONNECT;
static bool corrupt_config,fail_binding;
static unsigned allocations,worker_live,prepares,starts,config_writes,storage_writes,channel_writes,releases;
static esp32_mquickjs_wifi_radio_operation_t posted;
static void *binding_calloc(size_t count,size_t size) {
    assert(locks && !critical && count==1 && size==sizeof(wifi_radio_wps_t));
    if(fail_binding)return NULL;void *p=calloc(count,size);assert(p);allocations++;return p;
}
static void binding_free(void *p) {
    assert(allocations);for(size_t i=0;i<sizeof(wifi_radio_wps_t);i++)assert(!((uint8_t*)p)[i]);
    allocations--;free(p);
}
static int esp_wifi_sta_get_ap_info(wifi_ap_record_t *out) {
    assert(locks && !critical);memset(out,0,sizeof(*out));return peer_error;
}
static int esp_wifi_get_config(wifi_interface_t itf,wifi_config_t *out) {
    assert(locks && !critical && itf==WIFI_IF_STA);*out=actual;
    if(corrupt_config)out->sta.password[0]^=1;return 0;
}
static int (esp_wifi_set_config)(wifi_interface_t itf,const wifi_config_t *value) {
    assert(locks && !critical && itf==WIFI_IF_STA && storage==WIFI_STORAGE_RAM);
    config_writes++;if(config_error)return config_error;actual=*value;return 0;
}
static int (esp_wifi_set_storage)(wifi_storage_t value) {
    assert(locks && !critical && worker.prepared);storage_writes++;
    if(storage_error)return storage_error;storage=value;return 0;
}
static int wifi_radio_get_channel_locked(uint8_t *primary,wifi_second_chan_t *second,uint32_t *generation) {
    assert(locks && !critical);*primary=channel;*second=secondary;*generation=1;return 0;
}
static int wifi_radio_validate_regulatory_channel(uint8_t primary) {
    assert(locks && !critical);return primary==6?0:ESP_ERR_INVALID_ARG;
}
static int (esp_wifi_set_channel)(uint8_t primary,wifi_second_chan_t second) {
    assert(locks && !critical);channel_writes++;if(channel_error)return channel_error;
    channel=primary;secondary=second;return 0;
}
static int esp_event_post(int base,int id,const void *data,size_t size,unsigned wait) {
    assert(locks && !critical && base==ESP32QJS_WIFI_RADIO_CONTROL_EVENT && id==8 && size==sizeof(posted) && !wait);
    if(fence_error)return fence_error;memcpy(&posted,data,size);return 0;
}
esp_err_t esp32_mquickjs_wifi_wps_worker_create(const esp_wps_config_t *config,esp32_mquickjs_wifi_wps_worker_t **out) {
    assert(!locks && !critical && !*out && !worker_live);memset(&worker,0,sizeof(worker));
    if(config->wps_type!=WPS_TYPE_PIN)return ESP_ERR_INVALID_ARG;
    worker_live++;*out=&worker;return 0;
}
esp_err_t esp32_mquickjs_wifi_wps_worker_prepare(esp32_mquickjs_wifi_wps_worker_t *w) {
    assert(locks && !critical && w==&worker && storage==WIFI_STORAGE_FLASH);
    prepares++;worker.prepared=true;return prepare_error;
}
esp_err_t esp32_mquickjs_wifi_wps_worker_start(esp32_mquickjs_wifi_wps_worker_t *w) {
    assert(locks && !critical && w->prepared && storage==WIFI_STORAGE_RAM);
    starts++;memcpy(actual.sta.ssid,"negotiation",12);memset(actual.sta.password,0,sizeof(actual.sta.password));channel=11;
    return start_error;
}
esp_err_t esp32_mquickjs_wifi_wps_worker_status(esp32_mquickjs_wifi_wps_worker_t *w,esp32_mquickjs_wifi_wps_worker_status_t *out) {
    assert(locks && !critical && w==&worker);*out=w->status;return 0;
}
esp_err_t esp32_mquickjs_wifi_wps_worker_finish_capture(esp32_mquickjs_wifi_wps_worker_t *w) {
    assert(locks && !critical && w==&worker);if(close_error)return close_error;
    w->status.capture_retired=true;return 0;
}
esp_err_t esp32_mquickjs_wifi_wps_worker_close(esp32_mquickjs_wifi_wps_worker_t *w) {
    assert(locks && !critical && w==&worker);if(close_error)return close_error;
    w->status.capture_retired=w->status.retired=true;return 0;
}
esp_err_t esp32_mquickjs_wifi_wps_worker_release(esp32_mquickjs_wifi_wps_worker_t **w) {
    assert(locks && !critical && *w==&worker && worker.status.retired && worker_live);
    releases++;worker_live--;*w=NULL;return 0;
}
esp_err_t esp32_mquickjs_wifi_wps_worker_pin(esp32_mquickjs_wifi_wps_worker_t *w,uint8_t pin[8],bool commit) {
    assert(locks && !critical && w==&worker);if(!commit)memcpy(pin,"12345670",8);return 0;
}
esp_err_t esp32_mquickjs_wifi_wps_worker_credentials(esp32_mquickjs_wifi_wps_worker_t *w,esp32_mquickjs_wifi_wps_credentials_t *out,bool commit) {
    assert(locks && !critical && w==&worker && w->status.capture_retired && storage==WIFI_STORAGE_FLASH);
    if(!commit){memset(out,0,sizeof(*out));out->count=1;}return 0;
}
