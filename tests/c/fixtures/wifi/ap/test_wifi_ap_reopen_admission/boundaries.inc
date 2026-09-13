
#define WIFI_RADIO_NAN_PENDING false
#define WIFI_RADIO_MESH_PENDING false
/* No competing wake-interval or TX-rate transaction is active in these AP
 * admission cases; production owner admission remains in the extracted code. */
static struct {struct {unsigned identity;} owner;bool uncertain;} s_interval;
static struct {unsigned identity;} s_tx_rate_lease;
#define CONFIG_ESP_WIFI_SOFTAP_SUPPORT 1
#define ESP32_MQUICKJS_WIFI_MAX_AP_CLIENTS 4
#define ESP32_MQUICKJS_WIFI_AP_BEACON_QUANTUM_TU 100
#define ESP32_MQUICKJS_WIFI_AP_BEACON_MAX_TU 60000
#define ESP32_MQUICKJS_WIFI_AP_DTIM_MAX 10
#define ESP32_MQUICKJS_WIFI_RADIO_STARTED 3
#define AP_STOP_IDLE 0
static struct {
    int lock,driver_state,ap_stop_phase;bool started,driver_owned,storage_configured,restart_required;
    bool promiscuous_claimed,ap_reopen_pending,ap_reopen_attempted,ap_reopen_config_pending;
    bool ap_reopen_quiesced,ap_reopen_restore_complete;
    const char *fault_stage,*cleanup_stage;int fault_error;
    wifi_mode_t effective_mode,event_live;
    unsigned generation,next_lease_identity,next_lifecycle_identity,wake_locks;
    unsigned ap_transition_application,ap_transition_station,ap_transition_access_point;
    unsigned clients[ESP32_MQUICKJS_WIFI_RADIO_CLIENT_COUNT];
    struct { unsigned identity; } operation;
    esp32_mquickjs_wifi_radio_lifecycle_t lifecycle;
    wifi_radio_live_lease_t leases[WIFI_RADIO_MAX_LEASES];
} s_radio;
static int depth,critical,queries,allocs,live_alloc;
static bool fail_alloc,getter_error;
static wifi_config_t stored;
static int captures,capture_error;
static int esp32_mquickjs_wifi_ap_prestart_prepare(uint32_t generation,uint32_t identity,
    const wifi_config_t*requested,const wifi_config_t*previous,
    bool(*accept)(const wifi_config_t*,const wifi_config_t*)){
    assert(depth&&!critical&&generation==9&&identity==41&&s_radio.lifecycle.identity==41);
    assert(requested->ap.ssid[0]=='t'&&previous->ap.ssid[0]=='o'&&accept);++captures;return capture_error;
}
static int esp32_mquickjs_wifi_ap_prestart_release(uint32_t generation,uint32_t identity){
    (void)generation;(void)identity;assert(!"unexpected capture release");return 0;
}
static int wifi_radio_cleanup_fault(const char*stage,int error){(void)stage;assert(!"unexpected cleanup fault");return error;}
static void wifi_radio_operation_lock(void) { assert(!depth && !critical);depth=1; }
static void wifi_radio_operation_unlock(void) { assert(depth && !critical);depth=0; }
#define taskENTER_CRITICAL(p) do { (void)(p);assert(!critical);critical=1; } while(0)
#define taskEXIT_CRITICAL(p) do { (void)(p);assert(critical);critical=0; } while(0)
static void *checked_calloc(size_t n,size_t size) {
    assert(depth && !critical && n==1 && size==sizeof(wifi_config_t));allocs++;
    if(fail_alloc)return NULL;live_alloc++;return calloc(n,size);
}
static void checked_free(void *p) {
    assert(depth && !critical && live_alloc==1);
    for(size_t i=0;i<sizeof(wifi_config_t);i++)assert(!((unsigned char *)p)[i]);
    live_alloc--;free(p);
}
#define calloc checked_calloc
#define free checked_free
static int esp_wifi_get_mode(wifi_mode_t *m) { assert(depth && !critical);queries++;*m=WIFI_MODE_STA;return 0; }
static int wifi_radio_validate_regulatory_channel(uint8_t channel) { assert(depth&&!critical&&channel);return 0; }
static int esp_wifi_get_config(wifi_interface_t interface,wifi_config_t *c) {
    assert(depth && !critical && interface==WIFI_IF_AP);queries++;*c=stored;return getter_error?-7:0;
}
