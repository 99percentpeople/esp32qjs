
/* SDK storage shapes at injected driver boundaries, not a WPS state machine. */
typedef struct {struct {
    u8 ssid[32],password[64];
    struct {int authmode;} threshold;
    int bssid_set,sae_pwe_h2e;
    unsigned untouched;
} sta;} wifi_config_t;
static struct wps_sm {struct wps_credential creds[MAX_CRED_COUNT];u8 ap_cred_cnt;} native;
static bool missing_owner;
static struct wps_sm *wps_sm_get(void) {return missing_owner?NULL:&native;}
static wifi_config_t saved,installed;
static unsigned calls[4],allocated,dirty_frees;
static bool fail_alloc;
static int get_error,disconnect_error,set_error;
static void *allocation;
static void *os_zalloc(size_t n) {
    calls[0]++;if(fail_alloc)return NULL;
    assert(n==sizeof(wifi_config_t) && !allocation);
    allocation=calloc(1,n);assert(allocation);allocated++;return allocation;
}
static void os_free(void *p) {
    if(!p)return;
    assert(p==allocation && allocated==1);
    for(size_t i=0;i<sizeof(wifi_config_t);i++)if(((u8 *)p)[i]){dirty_frees++;break;}
    free(p);allocation=NULL;allocated--;
}
static int esp_wifi_get_config(int iface,wifi_config_t *config) {
    assert(iface==WIFI_IF_STA);calls[1]++;*config=saved;return get_error;
}
static int esp_wifi_disconnect(void) {calls[2]++;return disconnect_error;}
static int esp_wifi_set_config(int iface,wifi_config_t *config) {
    assert(iface==WIFI_IF_STA);calls[3]++;installed=*config;return set_error;
}
static void reset(void) {
    assert(!allocated);memset(calls,0,sizeof(calls));
    fail_alloc=false;get_error=disconnect_error=set_error=0;
    memset(&saved,0x55,sizeof(saved));saved.sta.threshold.authmode=7;
    memset(&installed,0,sizeof(installed));
}
