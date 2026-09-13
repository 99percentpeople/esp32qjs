
#define WIFI_RADIO_MAX_LEASES 4
#define ESP32_MQUICKJS_WIFI_RADIO_STOPPED 1
#define ESP32_MQUICKJS_WIFI_RADIO_CLEANUP_PENDING 2
#define ESP32_MQUICKJS_WIFI_RADIO_FAULTED 3
static unsigned lock_depth,critical_depth,writes;
static int replies[2];
static wifi_tx_rate_config_t observed[2];
static void wifi_radio_operation_lock(void) { assert(!lock_depth); ++lock_depth; }
static void wifi_radio_operation_unlock(void) { assert(lock_depth==1); --lock_depth; }
#define taskENTER_CRITICAL(lock) do { (void)(lock); assert(!critical_depth); ++critical_depth; } while(0)
#define taskEXIT_CRITICAL(lock) do { (void)(lock); assert(critical_depth==1); --critical_depth; } while(0)
static struct {
    int lock,driver_state;
    bool driver_owned,storage_configured,started,stop_required,restart_required,promiscuous_claimed;
    uint32_t wake_locks,generation;
    struct { uint32_t identity; } lifecycle,operation,leases[WIFI_RADIO_MAX_LEASES];
    const char *fault_stage,*cleanup_stage;
    esp_err_t fault_error,cleanup_error;
} s_radio={.driver_state=ESP32_MQUICKJS_WIFI_RADIO_STOPPED,.driver_owned=true,.storage_configured=true,.generation=7};
static esp32_mquickjs_wifi_tx_rate_state_t s_tx_rates={.next_identity=1};
static const char s_tx_rate_fault[]="tx-rate-uncertain";
static esp32_mquickjs_wifi_tx_rate_lease_t s_tx_rate_lease;
static esp_err_t esp_wifi_config_80211_tx(wifi_interface_t interface,wifi_tx_rate_config_t *config) {
    assert(lock_depth==1 && !critical_depth && writes<2);
    assert(interface==WIFI_IF_STA || interface==WIFI_IF_AP);
    observed[writes]=*config;
    /* Native pointer is temporary: SDK modification cannot corrupt the ledger. */
    config->rate=WIFI_PHY_RATE_1M_L;
    return replies[writes++];
}
