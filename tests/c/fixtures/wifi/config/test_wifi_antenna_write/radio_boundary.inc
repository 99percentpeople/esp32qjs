
#define WIFI_RADIO_MAX_LEASES 16
#define ESP_ERR_WIFI_NOT_INIT 5
static int radio_lock;
static struct {
 bool driver_owned,storage_configured,started,stop_required,restart_required,promiscuous_claimed;
 esp32_mquickjs_wifi_radio_driver_state_t driver_state;
 const char *fault_stage,*cleanup_stage;
 struct{unsigned identity;}lifecycle,operation,leases[16];
 unsigned wake_locks;
 esp32_mquickjs_wifi_radio_config_result_t configuration;
}s_radio;
static struct{unsigned identity;bool restore_pending;}s_tx_rate_lease;
#define taskENTER_CRITICAL(lock) ((void)0)
#define taskEXIT_CRITICAL(lock) ((void)0)
static void wifi_radio_operation_lock(void){assert(!radio_lock);radio_lock=1;}
static void wifi_radio_operation_unlock(void){assert(radio_lock);radio_lock=0;}
static int wifi_radio_record_fault(const char *stage,int error){assert(radio_lock);s_radio.fault_stage=stage;return error;}
