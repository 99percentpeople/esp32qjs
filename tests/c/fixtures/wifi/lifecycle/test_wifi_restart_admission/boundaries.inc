
static struct {
    int lock,driver_state;
    bool driver_owned,storage_configured,started,stop_required,stop_submitted,restart_required,promiscuous_claimed;
    const char *fault_stage,*cleanup_stage;
    unsigned generation,event_identity,next_lifecycle_identity,wake_locks,event_phase,event_live;
    wifi_mode_t effective_mode;wifi_storage_t storage;
    esp32_mquickjs_wifi_radio_lifecycle_t lifecycle;
    struct {unsigned identity;} operation;
    struct {unsigned identity;esp32_mquickjs_wifi_radio_client_t client;} leases[WIFI_RADIO_MAX_LEASES];
} s_radio;
static esp32_mquickjs_wifi_radio_stop_snapshot_t s_stop_snapshot;
#define ESP32_MQUICKJS_WIFI_POLICY_SLOT_AP_11B 2
#define CONFIG_ESP_WIFI_FTM_ENABLE 1
#define CONFIG_ESP_WIFI_FTM_RESPONDER_SUPPORT 1
static struct {struct {bool configured;} records[4];} s_policies;
static struct {bool configured;} s_ftm_offset;
static struct {unsigned identity;bool restore_pending;} s_tx_rate_lease;
static struct {struct {unsigned identity;} owner;bool restore_pending;} s_interval;
static struct {void *snapshot;} s_config_restart;
static struct {struct {unsigned identity;} owner;} s_policy_restart;
static struct {struct {unsigned identity;} start_owner;} s_vendor_ie;
enum {RADIO_EVENTS_IDLE=0};
static bool native_pending[6];
#define WIFI_RADIO_SMARTCONFIG_PENDING native_pending[0]
#define WIFI_RADIO_WPS_PENDING native_pending[1]
#define WIFI_RADIO_DPP_PENDING native_pending[2]
#define WIFI_RADIO_EAP_PENDING native_pending[3]
#define WIFI_RADIO_NAN_PENDING native_pending[4]
#define WIFI_RADIO_MESH_PENDING native_pending[5]
static unsigned locked,critical,lock_calls;
static void (*on_lock)(void);
static void wifi_radio_operation_lock(void) {
    assert(!locked && !critical);locked=1;++lock_calls;
    if(on_lock){void (*change)(void)=on_lock;on_lock=NULL;change();}
}
static void wifi_radio_operation_unlock(void) {assert(locked && !critical);locked=0;}
#define taskENTER_CRITICAL(p) do {(void)(p);assert(locked && !critical);critical=1;} while(0)
#define taskEXIT_CRITICAL(p) do {(void)(p);assert(locked && critical);critical=0;} while(0)
static void setup(void) {
    assert(!locked && !critical);memset(&s_radio,0,sizeof(s_radio));
    memset(&s_stop_snapshot,0,sizeof(s_stop_snapshot));memset(&s_tx_rate_lease,0,sizeof(s_tx_rate_lease));
    memset(&s_interval,0,sizeof(s_interval));memset(&s_config_restart,0,sizeof(s_config_restart));
    memset(&s_policy_restart,0,sizeof(s_policy_restart));memset(&s_vendor_ie,0,sizeof(s_vendor_ie));
    memset(native_pending,0,sizeof(native_pending));on_lock=NULL;lock_calls=0;
    memset(&s_policies,0,sizeof(s_policies));memset(&s_ftm_offset,0,sizeof(s_ftm_offset));
    s_radio.generation=7;s_radio.event_identity=19;s_radio.next_lifecycle_identity=31;
    s_radio.driver_owned=s_radio.storage_configured=true;s_radio.storage=WIFI_STORAGE_RAM;
    s_radio.driver_state=ESP32_MQUICKJS_WIFI_RADIO_STOPPED;s_radio.effective_mode=WIFI_MODE_STA;
    s_stop_snapshot.generation=7;s_stop_snapshot.stop_identity=19;
    s_stop_snapshot.step=ESP32_MQUICKJS_WIFI_STOP_SNAPSHOT_COMPLETE;
    s_stop_snapshot.unchanged=true;s_stop_snapshot.mode=WIFI_MODE_STA;
}
