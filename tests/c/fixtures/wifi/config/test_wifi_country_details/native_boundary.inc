
#define ESP_ERR_WIFI_NOT_INIT -7
#define ESP_ERR_WIFI_NOT_CONNECT -8
#define ESP32_MQUICKJS_WIFI_RADIO_STARTED 3
static struct {
    int lock,driver_state,fault_error;unsigned generation,wake_locks;
    bool driver_owned,storage_configured,started,stop_required,restart_required,promiscuous_claimed;
    const char *fault_stage,*cleanup_stage;wifi_mode_t effective_mode;
    struct {unsigned identity;} lifecycle,operation;
    struct {unsigned identity;esp32_mquickjs_wifi_radio_client_t client;} leases[WIFI_RADIO_MAX_LEASES];
    unsigned clients[ESP32_MQUICKJS_WIFI_RADIO_CLIENT_COUNT];
    esp32_mquickjs_wifi_radio_config_result_t configuration;
} s_radio;
static struct {unsigned identity;bool restore_pending;} s_tx_rate_lease;
static int depth,critical,calls,writes,fail_at,fail_second,corrupt_at;
static bool associated;static wifi_country_t native_country;
static void wifi_radio_operation_lock(void){assert(!depth&&!critical);depth=1;}
static void wifi_radio_operation_unlock(void){assert(depth&&!critical);depth=0;}
#define taskENTER_CRITICAL(p) do{(void)(p);assert(!critical);critical=1;}while(0)
#define taskEXIT_CRITICAL(p) do{(void)(p);assert(critical);critical=0;}while(0)
static int step(bool write){assert(depth&&!critical);calls++;writes+=write;return calls==fail_at?-77:calls==fail_second?-88:0;}
static int wifi_radio_record_fault(const char *stage,int e){s_radio.fault_stage=stage;s_radio.fault_error=e;return e;}
static int wifi_radio_cleanup_fault(const char *stage,int e){s_radio.cleanup_stage=stage;return e;}
static int esp_wifi_sta_get_ap_info(wifi_ap_record_t *ap){(void)ap;int e=step(false);return e?e:associated?ESP_OK:ESP_ERR_WIFI_NOT_CONNECT;}
static int esp_wifi_get_country(wifi_country_t *c){int e=step(false);if(!e){*c=native_country;if(calls==corrupt_at)c->cc[0]='Z';}return e;}
static int esp_wifi_set_country(const wifi_country_t *c){
 int e=step(true);int8_t power=native_country.max_tx_power;native_country=*c;
 native_country.max_tx_power=power;if(!native_country.cc[2])native_country.cc[2]=' ';return e;
}
static int esp_wifi_set_country_code(const char *code,bool automatic){
 int e=step(true);native_country.cc[0]=code[0];native_country.cc[1]=code[1];native_country.cc[2]=' ';
 native_country.schan=1;native_country.nchan=13;
 native_country.policy=automatic?WIFI_COUNTRY_POLICY_AUTO:WIFI_COUNTRY_POLICY_MANUAL;return e;
}
/* The production composite helpers contain PHY branches; country-only calls
 * must never enter any of them. */
static int unexpected(void){assert(!"unexpected non-country SDK call");return -99;}
#define esp_wifi_get_band_mode(...) unexpected()
#define esp_wifi_get_protocols(...) unexpected()
#define esp_wifi_get_bandwidths(...) unexpected()
#define esp_wifi_get_protocol(...) unexpected()
#define esp_wifi_get_bandwidth(...) unexpected()
#define esp_wifi_set_protocols(...) unexpected()
#define esp_wifi_set_bandwidths(...) unexpected()
#define esp_wifi_set_protocol(...) unexpected()
#define esp_wifi_set_bandwidth(...) unexpected()
#define esp_wifi_get_ps(...) unexpected()
#define esp_wifi_set_ps(...) unexpected()
