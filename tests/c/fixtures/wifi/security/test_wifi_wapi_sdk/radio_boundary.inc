
#define ESP_ERR_INVALID_ARG 258
#define WIFI_RADIO_MAX_LEASES 2
#define ESP32_MQUICKJS_WIFI_RADIO_UNINITIALIZED 0
typedef int wifi_mode_t;
typedef struct{uint32_t identity,generation;} esp32_mquickjs_wifi_radio_lifecycle_t;
static struct{
 esp32_mquickjs_wifi_radio_lifecycle_t lifecycle,operation,leases[WIFI_RADIO_MAX_LEASES];
 const char*fault_stage,*cleanup_stage;unsigned wake_locks;
 bool restart_required,driver_owned,promiscuous_claimed;int driver_state;
}s_radio;
static bool radio_locked,snapshot_unchanged,checkpoint_exact,stopped_fenced;
static void wifi_radio_operation_lock(void){assert(!radio_locked);radio_locked=true;}
static void wifi_radio_operation_unlock(void){assert(radio_locked);radio_locked=false;}
static bool wifi_radio_stop_snapshot_unchanged_locked(void){assert(radio_locked);return snapshot_unchanged;}
static bool wifi_radio_restart_checkpoint_matches_locked(const esp32_mquickjs_wifi_radio_lifecycle_t*t,wifi_mode_t mode){
 assert(radio_locked&&t->identity==41&&mode==1);return checkpoint_exact;
}
static int wifi_radio_check_stopped_lifecycle_locked(const esp32_mquickjs_wifi_radio_lifecycle_t*t,bool detached){
 assert(radio_locked&&t->identity==41&&detached);return stopped_fenced?ESP_OK:ESP_ERR_INVALID_STATE;
}
