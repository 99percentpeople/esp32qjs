
#define ESP_ERR_WIFI_NOT_INIT -7
#define RADIO_EVENTS_IDLE 0
#define WIFI_SECOND_CHAN_NONE 0
#if CONFIG_ESP_WIFI_SOFTAP_SUPPORT
#define EXPECTED_MODE WIFI_MODE_APSTA
#else
#define EXPECTED_MODE WIFI_MODE_STA
#endif
static struct {
 int lock,driver_state,fault_error,cleanup_error,channel_observation_error;
 bool driver_owned,storage_configured,started,stop_required,stop_submitted,restart_required,promiscuous_claimed;
 unsigned generation,channel_generation,event_phase,event_live,wake_locks,primary_channel,secondary_channel;
 wifi_storage_t storage;wifi_mode_t effective_mode;
 const char *fault_stage,*cleanup_stage;
 struct {unsigned identity;} lifecycle,operation,leases[WIFI_RADIO_MAX_LEASES];
 esp32_mquickjs_wifi_radio_config_result_t configuration;
} s_radio;
static struct {unsigned identity;bool restore_pending;} s_tx_rate_lease;
static struct {struct {unsigned identity;} owner;bool restore_pending;} s_interval;
static struct {struct {unsigned identity;} start_owner;} s_vendor_ie;
static struct {void *snapshot;} s_config_restart;
static struct {struct {unsigned identity;} owner;} s_policy_restart;
static unsigned s_policies,s_tx_rates,s_inactive_history,s_scan_parameters;
static unsigned pending;
#define WIFI_RADIO_SMARTCONFIG_PENDING (pending&1)
#define WIFI_RADIO_WPS_PENDING (pending&2)
#define WIFI_RADIO_DPP_PENDING (pending&4)
#define WIFI_RADIO_EAP_PENDING (pending&8)
#define WIFI_RADIO_NAN_PENDING (pending&16)
#define WIFI_RADIO_MESH_PENDING (pending&32)
static int depth,critical,calls,fail_at,invalidated;
static bool unchanged;static wifi_mode_t native_mode;
static void wifi_radio_operation_lock(void){assert(!depth&&!critical);depth=1;}
static void wifi_radio_operation_unlock(void){assert(depth&&!critical);depth=0;}
#define taskENTER_CRITICAL(p) do{(void)(p);assert(!critical);critical=1;}while(0)
#define taskEXIT_CRITICAL(p) do{(void)(p);assert(critical);critical=0;}while(0)
static int step(void){assert(depth&&!critical);return ++calls==fail_at?-77:ESP_OK;}
static int native_restore(void){return step();}
static void wifi_radio_invalidate_stop_snapshot_locked(void){assert(depth&&!critical);unchanged=false;}
static int esp_wifi_get_mode(wifi_mode_t *mode){int e=step();*mode=native_mode;return e;}
static int esp_wifi_set_storage(wifi_storage_t storage){assert(storage==s_radio.storage);return step();}
static bool esp32_mquickjs_wifi_interval_invalidate(void *state,unsigned generation){assert(state==&s_interval&&generation==9);invalidated++;return true;}
static void esp32_mquickjs_wifi_policy_invalidate(void *state){assert(state==&s_policies);invalidated++;}
static void esp32_mquickjs_wifi_tx_rate_invalidate(void *state){assert(state==&s_tx_rates);invalidated++;}
#define esp_wifi_restore() (wifi_radio_invalidate_stop_snapshot_locked(),native_restore())
static void reset(void){
 assert(!depth&&!critical);memset(&s_radio,0,sizeof(s_radio));
 memset(&s_tx_rate_lease,0,sizeof(s_tx_rate_lease));memset(&s_interval,0,sizeof(s_interval));
 memset(&s_vendor_ie,0,sizeof(s_vendor_ie));memset(&s_config_restart,0,sizeof(s_config_restart));
 memset(&s_policy_restart,0,sizeof(s_policy_restart));pending=calls=fail_at=invalidated=0;unchanged=true;
 s_radio.driver_owned=s_radio.storage_configured=true;s_radio.driver_state=ESP32_MQUICKJS_WIFI_RADIO_STOPPED;
 s_radio.generation=9;s_radio.storage=WIFI_STORAGE_FLASH;s_radio.effective_mode=WIFI_MODE_STA;
 native_mode=EXPECTED_MODE;s_inactive_history=99;s_radio.primary_channel=6;
}
