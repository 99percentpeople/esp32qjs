
typedef int esp_err_t,wifi_mode_t,wifi_ps_type_t,wifi_second_chan_t;
#define ESP_OK 0
#define ESPNOW_ADDRESS_BYTES 6
#define WIFI_SCAN_BSSID_STR_LEN 18
#define WIFI_SCAN_MAX_RESULTS 32
#define ESP32_MQUICKJS_WIFI_MAX_SCAN_RECORDS 32
#define ESP32_MQUICKJS_WIFI_SSID_MAX_LEN 32
#define MACSTR "%02x:%02x:%02x:%02x:%02x:%02x"
#define MAC2STR(a) a[0],a[1],a[2],a[3],a[4],a[5]
#define ESP32_MQUICKJS_WIFI_RADIO_CLIENT_WIFI_STA 0
#define ESP32_MQUICKJS_WIFI_RADIO_CLIENT_ESPNOW 1
#define ESP32_MQUICKJS_WIFI_RADIO_CLIENT_CSI 2
#define ESP32_MQUICKJS_WIFI_RADIO_CLIENT_APPLICATION 3
#define ESP32_MQUICKJS_WIFI_RADIO_CLIENT_WIFI_AP 4
#define ESP32_MQUICKJS_WIFI_RADIO_CLIENT_MONITOR 5
#define ESP32_MQUICKJS_WIFI_RADIO_CLIENT_RAW_TX 6
#define ESP32_MQUICKJS_WIFI_RADIO_CLIENT_VENDOR_IE 7
#define ESP32_MQUICKJS_WIFI_RADIO_CLIENT_ACTION 8
#define ESP32_MQUICKJS_WIFI_RADIO_CLIENT_COUNT 9
#define WIFI_SECOND_CHAN_NONE 0
#define ESP_NOW_MAX_DATA_LEN 250
#define ESP32_MQUICKJS_WIRELESS_TX_RECOVERING 1
#define ESP32_MQUICKJS_WIRELESS_TX_FAILED 2
#define ESP32_MQUICKJS_ESPNOW_TX_DROP_OLDEST_BATCH 1
#define ESP32_MQUICKJS_ESPNOW_TX_SLOT_NONE 255
#define portENTER_CRITICAL(p) ((void)(p))
#define portEXIT_CRITICAL(p) ((void)(p))
typedef struct { bool configured,short_guard_interval,ersu,dcm;unsigned phy_mode,mcs; } espnow_peer_rate_config_t;
typedef struct { uint8_t address[6];unsigned channel;bool encrypted;espnow_peer_rate_config_t rate_config; } espnow_peer_slot_t;
typedef enum {WIFI_PHY_MODE_LR,WIFI_PHY_MODE_11B,WIFI_PHY_MODE_11G,WIFI_PHY_MODE_11A,
    WIFI_PHY_MODE_HT20,WIFI_PHY_MODE_HT40,WIFI_PHY_MODE_HE20,WIFI_PHY_MODE_VHT20} wifi_phy_mode_t;
/* SNAPSHOT_TYPES */
typedef struct {
    esp32_mquickjs_wifi_radio_config_result_t configuration,activation;
    int driver_state,requested_mode,cleanup_error,channel_observation_error,wake_lock_error;unsigned wake_locks,active_operations,fixed_channel_owners,conflicted_channel_owners,promiscuous_owners;bool promiscuous_identity_exhausted;const char *cleanup_stage;
    unsigned generation,channel_generation,primary_channel;unsigned clients[9];
    unsigned event_phase,event_identity,event_expected,event_seen,event_stopped,event_live;bool event_fence_pending;
    bool lifecycle_active,initialized,driver_owned,restart_required,starting,started,max_tx_power_available,power_save_available;
    const char *fault_stage;int fault_error,mode,storage,power_save,max_tx_power_quarter_dbm;
} esp32_mquickjs_wifi_radio_status_t;
static struct { atomic_uint dropped_driver_events; const char *cleanup_stage; int cleanup_error,sta_detach_error,radio_lease; } s_wifi_state = {.cleanup_stage="timer-delete",.cleanup_error=-2};
static const char *s_wifi_setup_stage="driver-events";
static int s_wifi_setup_error=-3;
static const char *esp32_mquickjs_wifi_radio_driver_state_name(int v) { (void)v;return "cleanup-pending"; }
static const char *wifi_radio_mode_to_string(int v) { (void)v;return "station"; }
static const char *wifi_power_save_to_string(int v) { (void)v;return "none"; }
static const char *wifi_reason_to_string(int v) { (void)v;return "fixture"; }
static const char *wifi_authmode_to_string(int v) { (void)v;return "wpa2-psk"; }
static const char *espnow_phy_mode_name(int v) { (void)v;return "ht"; }
static const char *esp_err_to_name(int v) { (void)v;return "ESP_FAIL"; }
static int esp32_mquickjs_wifi_get_status(esp32_mquickjs_wifi_status_t *s) { *s=(esp32_mquickjs_wifi_status_t){.initialized=true,.scan_draining=true,.scan_cleanup_error=-1,.connect_timer_error=-2,.connect_draining=true,.disconnect_cleanup_error=-3,.ssid="test-network",.connected=true,.link={.valid=true,.ssid="test-network",.ssid_len=12,.bssid={2,1,2,3,4,5},.channel=6,.aid=41}};return 0; }
static JSValue esp32_mquickjs_wifi_ap_status(JSContext *ctx) { return JS_NewObject(ctx); }
static JSValue esp32_mquickjs_wifi_action_status(JSContext *ctx) { return JS_NewObject(ctx); }
static JSValue esp32_mquickjs_wifi_raw_tx_status(JSContext *ctx) { return JS_NewObject(ctx); }
static JSValue esp32_mquickjs_wifi_watch_status(JSContext *ctx) { return JS_NewObject(ctx); }
static bool esp32_mquickjs_wifi_radio_rssi_request_status(esp32_mquickjs_wifi_rssi_request_t *s) {
    *s=(esp32_mquickjs_wifi_rssi_request_t){.generation=7,.revision=1,.requested_dbm=-70};return true;
}
static int esp32_mquickjs_wifi_radio_sample_station_link(const void *lease,const uint8_t bssid[6],
    uint8_t channel,esp32_mquickjs_wifi_link_sample_t *sample) {
    (void)lease;(void)bssid;(void)channel;
    *sample=(esp32_mquickjs_wifi_link_sample_t){.rssi_valid=true,.rssi=-50,.phy_valid=true,.phy=WIFI_PHY_MODE_HT20};return ESP_OK;
}
static int esp32_mquickjs_wifi_ap_netif_cleanup_error(void) { return -31; }
static int esp32_mquickjs_wifi_radio_get_status(esp32_mquickjs_wifi_radio_status_t *s) {
    *s=(esp32_mquickjs_wifi_radio_status_t){.configuration={.stage="mode",.error=-1,.mutation_attempted=true,.rollback_attempted=true,.rollback_complete=true},.activation={.stage="verify",.error=-2},.event_phase=5,.event_expected=1,.event_seen=0,.event_stopped=1,.event_fence_pending=true,.channel_observation_error=-3,.cleanup_stage="deinit",.cleanup_error=-2,.active_operations=1,.fixed_channel_owners=2,.conflicted_channel_owners=1,.generation=UINT32_MAX,.driver_owned=true,.restart_required=true,.fault_stage="start",.fault_error=-1,.primary_channel=6,.max_tx_power_available=true,.max_tx_power_quarter_dbm=71,.power_save_available=true,.clients={1,1,1}};return 0;
}
static int esp32_mquickjs_wifi_radio_get_channel(uint8_t *p,wifi_second_chan_t *s,uint32_t *g) { *p=6;*s=0;*g=1;return 0; }
typedef enum { WIFI_CIPHER_TYPE_NONE, WIFI_CIPHER_TYPE_WEP40, WIFI_CIPHER_TYPE_WEP104, WIFI_CIPHER_TYPE_TKIP, WIFI_CIPHER_TYPE_CCMP, WIFI_CIPHER_TYPE_TKIP_CCMP, WIFI_CIPHER_TYPE_AES_CMAC128, WIFI_CIPHER_TYPE_SMS4, WIFI_CIPHER_TYPE_GCMP, WIFI_CIPHER_TYPE_GCMP256, WIFI_CIPHER_TYPE_AES_GMAC128, WIFI_CIPHER_TYPE_AES_GMAC256 } wifi_cipher_type_t;
#define WIFI_STORAGE_RAM 1
#define WIFI_SECOND_CHAN_ABOVE 1
#define WIFI_SECOND_CHAN_BELOW 2
#define WIFI_ANT_ANT0 0
#define WIFI_ANT_ANT1 1
#define WIFI_COUNTRY_POLICY_AUTO 0
#define WIFI_COUNTRY_POLICY_MANUAL 1
#define CONFIG_SOC_WIFI_SUPPORT_5G 1
typedef struct { char cc[3];uint8_t schan,nchan;int8_t max_tx_power;int policy;uint32_t wifi_5g_channel_mask; } wifi_country_t;
typedef struct { uint8_t bssid[6],ssid[33];int rssi,primary,authmode,second,ant;wifi_cipher_type_t pairwise_cipher,group_cipher;wifi_country_t country;bool phy_11b,phy_11g,phy_11n,phy_11a,phy_11ac,phy_11ax,phy_lr,wps,ftm_responder,ftm_initiator; } wifi_ap_record_t;
static uint32_t esp32_mquickjs_wifi_radio_restart_snapshot_bytes(void) {return 0;}
static int driver_failure;
static void esp32_mquickjs_wifi_scan_results_consumed(void) {}
static int esp_wifi_scan_get_ap_num(uint16_t *n) { *n=2;return driver_failure==1 ? -1 : 0; }
static int esp_wifi_scan_get_ap_records(uint16_t *n,wifi_ap_record_t *r) {
    for(int i=0;i<*n;i++)r[i]=(wifi_ap_record_t){.ssid="test-network",.primary=6,.rssi=-50};
    return driver_failure==2 ? -1 : 0;
}
