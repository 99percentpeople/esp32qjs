
#define TAG "fixture"
#define ESP_LOGE(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
#define ESP_ERR_WIFI_NOT_CONNECT -4
#define ESP_ERR_TIMEOUT -5
#define ESP_ERR_INVALID_RESPONSE -6
#define WIFI_IF_STA 0
#define pdMS_TO_TICKS(ms) (ms)
#define ESP_RETURN_ON_ERROR(call,...) do { int wifi_test_error=(call);if(wifi_test_error) return wifi_test_error; } while(0)
static _Atomic uint32_t s_wifi_timeout_armed_generation,s_wifi_timeout_pending;
static int timer_stop_error,timer_start_error,connect_error,barriers,configs,connects,arms;
static unsigned counter_lock_depth;
static bool emit_old_timeout;
static bool wifi_publish_driver_event_from_callback(const esp32_mquickjs_wifi_driver_event_t *e);
static size_t strnlen(const char *s,size_t n) { size_t i=0;while(i<n && s[i]) i++;return i; }
static int esp_timer_stop(void *t) { assert(t && !lock_depth);return 0; }
static int esp_timer_stop_blocking(void *t,unsigned ticks) {
    assert(t && ticks && !lock_depth);barriers++;
    assert(atomic_load(&s_wifi_timeout_armed_generation)==0);
    if(emit_old_timeout) {
        const esp32_mquickjs_wifi_driver_event_t old={.kind=WIFI_DRIVER_EVENT_CONNECT_TIMEOUT,.generation=7};
        wifi_publish_driver_event_from_callback(&old);
    }
    return timer_stop_error;
}
static int esp_wifi_set_config(int interface,const wifi_config_t *config) { (void)interface;(void)config;assert(!lock_depth);configs++;return 0; }
static wifi_config_t stored_connect_config;
static int config_reads,config_read_error;
static int esp_wifi_get_config(int interface,wifi_config_t *config) { assert(interface==WIFI_IF_STA && !lock_depth);config_reads++;*config=stored_connect_config;return config_read_error; }
static bool esp32_mquickjs_wifi_radio_accept_station_config(const wifi_config_t *a,const wifi_config_t *b) { return !memcmp(a,b,sizeof(*a)); }
static int esp_wifi_connect(void) { assert(!lock_depth && !counter_lock_depth);connects++;return connect_error; }
static int esp_timer_start_once(void *t,uint64_t us) { assert(t && us && !lock_depth);arms++;return timer_start_error; }
static bool deliver_disconnect_on_wait;
static unsigned wifi_wait_for_bits(unsigned bits,bool clear,unsigned ms,bool *interrupted) {
    (void)bits;(void)clear;(void)ms;assert(!lock_depth);*interrupted=false;
    if(deliver_disconnect_on_wait) {
        const esp32_mquickjs_wifi_driver_event_t down={.kind=WIFI_DRIVER_EVENT_DISCONNECTED};
        const esp32_mquickjs_wifi_driver_event_t old_ip={.kind=WIFI_DRIVER_EVENT_GOT_IP};
        wifi_process_driver_event(&down);wifi_process_driver_event(&old_ip);
        const esp32_mquickjs_wifi_driver_event_t fence={.kind=WIFI_DRIVER_EVENT_LINK_DRAINED,.generation=fence_epoch};
        wifi_process_driver_event(&fence);
    }
    return event_bits;
}
static int xQueueReceive(void *q,void *event,int wait) { (void)q;(void)event;(void)wait;return 0; }
