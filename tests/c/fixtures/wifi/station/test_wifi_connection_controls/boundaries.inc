
#define ESP_OK 0
#define ESP_ERR_INVALID_ARG 1
#define ESP_ERR_INVALID_STATE 2
#define ESP_ERR_NOT_SUPPORTED 3
#define ESP_ERR_WIFI_NOT_INIT 4
#define ESP_ERR_WIFI_NOT_STARTED 5
#define ESP_ERR_INVALID_RESPONSE 6
#define ESP_ERR_NO_MEM 17
#define WIFI_RADIO_MAX_LEASES 6
static unsigned locks,critical,helper_locks,calls,writes,fail_at,rollback_fail;
static uint16_t inactive[2]={6,300};
static int threshold=-60;
static bool corrupt_readback;
typedef struct {uint32_t identity;esp32_mquickjs_wifi_radio_client_t client;} wifi_radio_live_lease_t;
static struct {
    int lock;uint32_t generation,wake_locks;
    bool driver_owned,storage_configured,started,promiscuous_claimed,restart_required;
    struct {unsigned identity;} operation,lifecycle;
    const char *fault_stage,*cleanup_stage;esp_err_t fault_error,cleanup_error;
    esp32_mquickjs_wifi_radio_driver_state_t driver_state;
    wifi_mode_t effective_mode;
    wifi_storage_t storage;
    wifi_radio_live_lease_t leases[WIFI_RADIO_MAX_LEASES];
    esp32_mquickjs_wifi_radio_config_result_t configuration;
} s_radio;
static struct {unsigned identity;} s_tx_rate_lease;
static esp32_mquickjs_wifi_radio_lease_t s_wifi_application,s_ap_lease;
static esp32_mquickjs_wifi_radio_lifecycle_t s_wifi_lifecycle;
static bool s_ap_cleanup_pending,s_wifi_configuration_cleanup,s_wifi_ap_stop_cleanup;
static struct {
    esp32_mquickjs_wifi_radio_lease_t radio_lease;
    bool runtime_cleanup_pending;
    const char *cleanup_stage;
    struct {bool connected;} status;
    bool connect_in_progress,connect_draining,connect_start_active,disconnect_active;
    bool scan_in_progress,scan_draining,scan_results_pending,scan_start_active,scan_stop_active;
    bool connect_future_registered,scan_future_registered;
} s_wifi_state;
static esp32_mquickjs_wifi_radio_config_result_t result;
static void wifi_radio_operation_lock(void) {assert(!locks && !helper_locks && !critical);++locks;}
static void wifi_radio_operation_unlock(void) {assert(locks==1 && !critical);--locks;}
static void wifi_lock(void) {assert(!locks && !helper_locks);++helper_locks;}
static void wifi_unlock(void) {assert(helper_locks==1);--helper_locks;}
#define taskENTER_CRITICAL(p) do {(void)(p);assert(!critical);++critical;} while(0)
#define taskEXIT_CRITICAL(p) do {(void)(p);assert(critical==1);--critical;} while(0)
static int sdk_step(bool write) {
    assert(locks==1 && !critical && !helper_locks);++calls;writes+=write;
    if(calls==fail_at)return 77;
    if(result.rollback_attempted && rollback_fail && --rollback_fail==0)return 88;
    return ESP_OK;
}
static int esp_wifi_get_inactive_time(wifi_interface_t iface,uint16_t *value)
{
    *value=inactive[iface];
    if(corrupt_readback && !strcmp(result.stage,"inactive-time-readback") && !result.rollback_attempted)*value+=1;
    return sdk_step(false);
}
static int esp_wifi_set_inactive_time(wifi_interface_t iface,uint16_t value)
{
    inactive[iface]=value;return sdk_step(true);
}
static int esp_wifi_set_rssi_threshold(int32_t value) {threshold=value;return sdk_step(true);}
