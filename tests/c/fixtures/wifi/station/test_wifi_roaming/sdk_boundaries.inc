
static int dispatch_failure,query_code,read_error,link_error;
static unsigned dispatches,submissions;
static bool in_sdk,ap_supported=true,btm_enabled=true;
static int esp_wifi_sta_get_ap_info(wifi_ap_record_t *ap) {assert(in_sdk);memset(ap,0,sizeof(*ap));return link_error;}
static bool esp_rrm_is_rrm_supported_connection(void) { assert(in_sdk);return ap_supported; }
static bool esp_wnm_is_btm_supported_connection(void) { assert(in_sdk);return ap_supported; }
static int esp_wifi_get_config(wifi_interface_t interface,wifi_config_t *config) {
    assert(in_sdk && interface==WIFI_IF_STA);memset(config,0xa5,sizeof(*config));config->sta.btm_enabled=btm_enabled;return read_error;
}
static int esp_wnm_send_bss_transition_mgmt_query(int reason,const char *text,int cached) {
    assert(in_sdk && !critical && locks && !cached && reason>=0 && reason<=9);
    if(text)assert(strstr(text,",-1,81,6,7,030100"));
    ++submissions;return query_code;
}
static int eloop_register_timeout_blocking(int (*handler)(void *,void *),void *arg,void *unused) {
    assert(locks && !critical && !in_sdk);++dispatches;
    if(dispatch_failure)return -1; /* Includes eloop destruction before dispatch. */
    in_sdk=true;int r=handler(arg,unused);in_sdk=false;return r;
}
