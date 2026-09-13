
#define CONFIG_ESP_WIFI_SOFTAP_SUPPORT 1
#define WIFI_RADIO_MAX_LEASES 4U
#define WIFI_RADIO_NAN_PENDING false
#define WIFI_RADIO_MESH_PENDING false
static esp32_mquickjs_wifi_interval_state_t s_interval;
#define ESP_ERR_WIFI_NOT_CONNECT 101
#define ESP_ERR_NOT_SUPPORTED 102
#define taskENTER_CRITICAL portENTER_CRITICAL
#define taskEXIT_CRITICAL portEXIT_CRITICAL
static struct { uint32_t identity,generation; } s_tx_rate_lease;
static struct {
    portMUX_TYPE lock;
    wifi_radio_live_lease_t leases[WIFI_RADIO_MAX_LEASES];
    uint32_t clients[ESP32_MQUICKJS_WIFI_RADIO_CLIENT_COUNT],generation,next_lease_identity;
    bool started;wifi_mode_t effective_mode;
    const char *fault_stage;esp_err_t fault_error;
    struct { uint32_t identity,lease_identity; } lifecycle,operation;
    uint8_t primary_channel;wifi_second_chan_t secondary_channel;
    uint32_t channel_generation;uint64_t channel_observation_revision;
    esp_err_t channel_observation_error;
} s_radio={.lock=portMUX_INITIALIZER_UNLOCKED,.generation=7,.next_lease_identity=123,
    .started=true,.effective_mode=WIFI_MODE_STA,.primary_channel=6};
static pthread_mutex_t operation_lock=PTHREAD_MUTEX_INITIALIZER;
static _Thread_local unsigned operation_depth;
static _Thread_local bool release_task;
static bool try_release,release_entered;
static pthread_t release_worker;
static esp32_mquickjs_wifi_radio_lease_t *releasing_lease;
static void wifi_radio_operation_lock(void) {
    assert(!critical_depth && !operation_depth);
    if(release_task) {
        assert(!pthread_mutex_lock(&pause_lock));release_entered=true;
        assert(!pthread_cond_broadcast(&changed));assert(!pthread_mutex_unlock(&pause_lock));
    }
    assert(!pthread_mutex_lock(&operation_lock));++operation_depth;
}
static void wifi_radio_operation_unlock(void) {
    assert(operation_depth==1 && !critical_depth);--operation_depth;
    assert(!pthread_mutex_unlock(&operation_lock));
}
static void wifi_radio_release_promiscuous_locked(esp32_mquickjs_wifi_radio_promiscuous_lease_t *lease) {
    (void)lease;assert(!"Raw TX must never own or release a promiscuous lease");
}
static wifi_ap_record_t connected_ap;
static wifi_sta_list_t connected_clients;
static bool station_connected;
static esp_err_t mode_error,association_error,mac_error,channel_error;
static wifi_mode_t driver_mode=WIFI_MODE_STA;
static uint8_t driver_channel=6;
static esp_err_t esp_wifi_get_mode(wifi_mode_t *mode) {
    assert(operation_depth==1 && !critical_depth);*mode=driver_mode;return mode_error;
}
static esp_err_t esp_wifi_sta_get_ap_info(wifi_ap_record_t *ap) {
    assert(operation_depth==1 && !critical_depth);*ap=connected_ap;
    return association_error ? association_error : station_connected ? ESP_OK : ESP_ERR_WIFI_NOT_CONNECT;
}
static esp_err_t esp_wifi_ap_get_sta_list(wifi_sta_list_t *clients) {
    assert(operation_depth==1 && !critical_depth);*clients=connected_clients;return association_error;
}
static esp_err_t esp_wifi_get_mac(wifi_interface_t interface,uint8_t *mac) {
    assert(operation_depth==1 && !critical_depth && (interface==WIFI_IF_STA || interface==WIFI_IF_AP));
    memcpy(mac,source_address,6);return mac_error;
}
static esp_err_t esp_wifi_get_channel(uint8_t *primary,wifi_second_chan_t *secondary) {
    assert(operation_depth==1 && !critical_depth);*primary=driver_channel;*secondary=WIFI_SECOND_CHAN_NONE;
    return channel_error;
}
