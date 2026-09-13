
#define CONFIG_ESP_WIFI_FTM_ENABLE 1
#define CONFIG_ESP_WIFI_FTM_INITIATOR_SUPPORT 1
static struct {
    int lock;
    unsigned generation,wake_locks,next_lease_identity;
    bool driver_owned,storage_configured,started,stop_required,restart_required,promiscuous_claimed;
    esp32_mquickjs_wifi_radio_driver_state_t driver_state;
    esp32_mquickjs_wifi_radio_lifecycle_t lifecycle;
    struct {unsigned identity;} operation,leases[WIFI_RADIO_MAX_LEASES];
    const char *fault_stage,*cleanup_stage;int fault_error,cleanup_error;
    esp32_mquickjs_wifi_radio_config_result_t configuration;
} s_radio;
typedef struct {bool restore_off;int capture_error;} frozen_t;
static frozen_t frozen;
static struct {
    bool captured;frozen_t *snapshot;unsigned source_generation;
    wifi_mode_t mode;esp32_mquickjs_wifi_radio_lifecycle_t owner;
} s_config_restart;
static struct {wifi_mode_t mode;esp32_mquickjs_wifi_radio_lifecycle_t owner;} s_policy_restart;
static struct {unsigned identity;bool restore_pending;} s_tx_rate_lease;
static struct {struct {unsigned identity;} owner;bool restore_pending;} s_interval;
static struct {struct {unsigned identity;} start_owner;} s_vendor_ie;
static bool native_pending[6],recovery[4];
#define WIFI_RADIO_SMARTCONFIG_PENDING native_pending[0]
#define WIFI_RADIO_WPS_PENDING native_pending[1]
#define WIFI_RADIO_DPP_PENDING native_pending[2]
#define WIFI_RADIO_EAP_PENDING native_pending[3]
#define WIFI_RADIO_NAN_PENDING native_pending[4]
#define WIFI_RADIO_MESH_PENDING native_pending[5]
static bool wifi_radio_raw_tx_recovery_exact_locked(const esp32_mquickjs_wifi_radio_lifecycle_t *t) {(void)t;return recovery[0];}
static bool wifi_radio_action_recovery_exact_locked(const esp32_mquickjs_wifi_radio_lifecycle_t *t) {(void)t;return recovery[1];}
static bool wifi_radio_ftm_recovery_exact_locked(const esp32_mquickjs_wifi_radio_lifecycle_t *t) {(void)t;return recovery[2];}
#if CONFIG_SOC_WIFI_HE_SUPPORT && CONFIG_IDF_TARGET_ESP32C5
static bool wifi_radio_twt_recovery_exact_locked(const esp32_mquickjs_wifi_radio_lifecycle_t *t) {(void)t;return recovery[3];}
#endif
static unsigned locked,critical,lock_calls;
static void (*on_lock)(void);
static void wifi_radio_operation_lock(void) {
    assert(!locked && !critical);locked=1;++lock_calls;
    if(on_lock){void (*change)(void)=on_lock;on_lock=NULL;change();}
}
static void wifi_radio_operation_unlock(void) {assert(locked && !critical);locked=0;}
#define taskENTER_CRITICAL(p) do {(void)(p);assert(locked && !critical);critical=1;} while(0)
#define taskEXIT_CRITICAL(p) do {(void)(p);assert(locked && critical);critical=0;} while(0)
static esp32_mquickjs_wifi_radio_lifecycle_t token;
static void setup(void) {
    assert(!locked && !critical);memset(&s_radio,0,sizeof(s_radio));
    memset(&s_config_restart,0,sizeof(s_config_restart));memset(&s_policy_restart,0,sizeof(s_policy_restart));
    memset(&s_tx_rate_lease,0,sizeof(s_tx_rate_lease));memset(&s_interval,0,sizeof(s_interval));
    memset(&s_vendor_ie,0,sizeof(s_vendor_ie));memset(native_pending,0,sizeof(native_pending));
    memset(recovery,0,sizeof(recovery));memset(&frozen,0,sizeof(frozen));on_lock=NULL;lock_calls=0;
    token=(esp32_mquickjs_wifi_radio_lifecycle_t){.identity=41,.generation=7};
    s_radio.lifecycle=token;s_radio.generation=9;s_radio.next_lease_identity=101;
    s_radio.driver_owned=s_radio.storage_configured=s_radio.started=s_radio.stop_required=true;
    s_radio.driver_state=ESP32_MQUICKJS_WIFI_RADIO_FAULTED;
    s_radio.fault_stage="start-events";s_radio.fault_error=77;
    s_radio.cleanup_stage="stop-events";s_radio.cleanup_error=88;
    s_radio.configuration=(esp32_mquickjs_wifi_radio_config_result_t){.stage="old-config",.error=66};
    s_config_restart.owner=s_policy_restart.owner=token;
    s_config_restart.mode=s_policy_restart.mode=WIFI_MODE_STA;
    s_config_restart.captured=true;s_config_restart.source_generation=7;s_config_restart.snapshot=&frozen;
}
