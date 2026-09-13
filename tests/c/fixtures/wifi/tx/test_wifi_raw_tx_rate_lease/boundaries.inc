
#define WIFI_RADIO_MAX_LEASES 4
#define WIFI_RADIO_NAN_PENDING false
#define WIFI_RADIO_MESH_PENDING false
#define ESP_ERR_NO_MEM 4
#define ESP_ERR_TIMEOUT 5
#define ESP_ERR_WIFI_NOT_STARTED 6
#define RADIO_EVENTS_IDLE 0
#define RADIO_EVENTS_STOP 2
#define AP_STOP_IDLE 0
static unsigned locks,critical,writes,starts,stops,waits;
static bool stop_observed;
static int next_write[2],stop_error,wait_error,start_error;
static wifi_tx_rate_config_t native_rate={.phymode=WIFI_PHY_MODE_11G,.rate=WIFI_PHY_RATE_6M};
static struct {
    int lock;
    wifi_radio_live_lease_t leases[WIFI_RADIO_MAX_LEASES];
    unsigned generation,next_lease_identity,clients[ESP32_MQUICKJS_WIFI_RADIO_CLIENT_COUNT];
    esp32_mquickjs_wifi_radio_driver_state_t driver_state;
    bool driver_owned,storage_configured,started,stop_required,stop_submitted,restart_required,promiscuous_claimed;
    unsigned wake_locks;
    struct {uint32_t identity,lease_identity;} operation,lifecycle;
    const char *fault_stage,*cleanup_stage;esp_err_t fault_error,cleanup_error;
    wifi_mode_t effective_mode,event_live;
    unsigned event_phase,ap_stop_phase,ap_transition_application,ap_transition_station,ap_transition_access_point;
    bool ap_reopen_pending,ap_reopen_attempted,ap_reopen_quiesced,ap_reopen_restore_complete;
    esp_err_t ap_stop_error;
} s_radio={.generation=7,.next_lease_identity=1,.driver_state=ESP32_MQUICKJS_WIFI_RADIO_STOPPED,
    .driver_owned=true,.storage_configured=true};
/* Unrelated connectionless owner is an injected admission boundary. */
static struct {
    struct {uint32_t identity,owner_identity,generation;} owner;
    bool uncertain,restore_pending;
} s_interval;
static esp32_mquickjs_wifi_tx_rate_state_t s_tx_rates={.next_identity=2};
static esp32_mquickjs_wifi_tx_rate_lease_t s_tx_rate_lease;
static const char s_tx_rate_restore_fault[]="tx-rate-restore";
static void wifi_radio_operation_lock(void) {assert(!locks);++locks;}
static void wifi_radio_operation_unlock(void) {assert(locks==1);--locks;}
#define taskENTER_CRITICAL(lock) do {(void)(lock);assert(!critical);++critical;} while(0)
#define taskEXIT_CRITICAL(lock) do {(void)(lock);assert(critical==1);--critical;} while(0)
static void wifi_radio_set_state(esp32_mquickjs_wifi_radio_driver_state_t state) {s_radio.driver_state=state;}
static void wifi_radio_release_promiscuous_locked(esp32_mquickjs_wifi_radio_promiscuous_lease_t *lease) {(void)lease;assert(0);}
static int wifi_radio_begin_events(unsigned phase,wifi_mode_t mode) {(void)mode;assert(locks && !critical);s_radio.event_phase=phase;stop_observed=false;return ESP_OK;}
/* Getter capture has its own production fixture. This boundary verifies that
 * the actual stop path observes before its first SDK submission. */
static void wifi_radio_capture_stop_snapshot_locked(void) {assert(locks && !critical && !stop_observed);stop_observed=true;}
static int wifi_radio_wait_events(void) {assert(locks && !critical);++waits;if(wait_error)return wait_error;s_radio.event_phase=RADIO_EVENTS_IDLE;return ESP_OK;}
static int esp_wifi_stop(void) {assert(locks && !critical && s_tx_rate_lease.identity && stop_observed);++stops;return stop_error;}
static int wifi_radio_validate_regulatory_channel(uint8_t channel) {assert(channel==6);return ESP_OK;}
static uint32_t esp32_mquickjs_wifi_radio_5ghz_channel_bit(uint8_t channel) {return channel==36?1:0;}
static int wifi_radio_get_channel_locked(uint8_t *channel,wifi_second_chan_t *secondary,uint32_t *revision) {*channel=6;*secondary=WIFI_SECOND_CHAN_NONE;*revision=1;return ESP_OK;}
static int wifi_radio_set_channel_locked(esp32_mquickjs_wifi_radio_lease_t *lease,uint8_t channel,wifi_second_chan_t secondary) {(void)secondary;assert(lease->acquired && channel==6);return ESP_OK;}
static int wifi_radio_ensure_started_locked(esp32_mquickjs_wifi_radio_lease_t *lease) {
    assert(locks && !critical && s_tx_rate_lease.identity==lease->identity && writes);
    ++starts;s_radio.stop_required=true;
    if(start_error){s_radio.fault_stage="start";s_radio.fault_error=start_error;return start_error;}
    s_radio.started=true;s_radio.effective_mode=WIFI_MODE_STA;s_radio.driver_state=ESP32_MQUICKJS_WIFI_RADIO_STARTED;
    return ESP_OK;
}
static esp_err_t esp_wifi_config_80211_tx(wifi_interface_t interface,wifi_tx_rate_config_t *config) {
    assert(locks && !critical && !s_radio.started && !s_radio.stop_required && interface==WIFI_IF_STA && s_tx_rate_lease.identity);
    ++writes;int error=next_write[0];next_write[0]=next_write[1];next_write[1]=0;
    native_rate=*config; /* Even a failure may have changed driver state. */
    return error;
}
