
#define WIFI_ALL_CHANNEL_SCAN 1
#define WIFI_AUTH_OPEN 0
#define WIFI_AUTH_OWE 9
#define WIFI_AUTH_WPA3_PSK 6
#define WIFI_AUTH_WPA2_WPA3_PSK 7
#define WIFI_AUTH_WAPI_PSK 8
#define WPA3_SAE_PWE_HUNT_AND_PECK 1
#define WPA3_SAE_PWE_HASH_TO_ELEMENT 2
#define WPA3_SAE_PWE_BOTH 3
#define WPA3_SAE_PK_MODE_AUTOMATIC 0
#define WPA3_SAE_PK_MODE_ONLY 1
#define WPA3_SAE_PK_MODE_DISABLED 2
typedef int wifi_sae_pwe_method_t;
typedef struct {
    uint16_t listen_interval;
    uint8_t failure_retry_cnt,channel,password[64],sae_h2e_identifier[32],ssid[32],bssid[6];
    int scan_method,sort_method,sae_pwe_h2e,sae_pk_mode;
    bool bssid_set,rm_enabled,btm_enabled,mbo_enabled,ft_enabled,owe_enabled;
    bool transition_disable,disable_wpa3_compatible_mode;
    uint32_t he_dcm_set:1,he_dcm_max_constellation_tx:2,he_dcm_max_constellation_rx:2;
    uint32_t he_mcs9_enabled:1,he_su_beamformee_disabled:1;
    uint32_t he_trig_su_bmforming_feedback_disabled:1,he_trig_mu_bmforming_partial_feedback_disabled:1;
    uint32_t he_trig_cqi_feedback_disabled:1,vht_su_beamformee_disabled:1,vht_mu_beamformee_disabled:1,vht_mcs8_enabled:1;
    struct { bool capable,required; } pmf_cfg;
    struct { int authmode;int8_t rssi;uint8_t rssi_5g_adjustment; } threshold;
} wifi_sta_config_t;
static int unsupported;
static const char *unsupported_option;
static bool wifi_station_unsupported(JSContext *ctx,const char *name,const char *operation) {
    assert(!strcmp(operation,"wifi.connect") || !strcmp(operation,"wifi.configure"));
    unsupported_option=name;unsupported++;
    JS_ThrowTypeError(ctx,"unsupported target option");return false;
}
/* Root the VM API's input before an injected moving collection. */
static JSValue get_property_with_fault(JSContext *ctx,JSValue object,const char *key) {
    JSGCRef r;JSValue *root=JS_PushGCRef(ctx,&r);*root=object;
    JSValue result=step(ctx)?JS_ThrowOutOfMemory(ctx):JS_GetPropertyStr(ctx,*root,key);
    JS_PopGCRef(ctx,&r);return result;
}
#define JS_GetPropertyStr get_property_with_fault
