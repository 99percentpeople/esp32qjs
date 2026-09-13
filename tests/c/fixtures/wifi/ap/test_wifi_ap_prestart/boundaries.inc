
static wifi_config_t stored;
static wifi_mode_t mode=WIFI_MODE_STA;
uint8_t g_ic[24];
static int mode_creates,ap_starts,setters,reads,fail_set,pmf_calls;
static bool wifi_task,bad_read,wrong_task,dispatch_failure,fail_pmf,fail_create;
static int esp_wifi_get_mode(wifi_mode_t*out){assert(!critical);*out=mode;return 0;}
static int esp_wifi_get_config(wifi_interface_t interface,wifi_config_t*out){
 assert(!critical&&wifi_task&&interface==WIFI_IF_AP);++reads;*out=stored;
 if(bad_read){out->ap.ssid[0]='?';bad_read=false;}return 0;
}
static int esp_wifi_set_mode(wifi_mode_t mode);
static int esp_wifi_disable_pmf_config(wifi_interface_t interface){
 assert(!critical&&wifi_task&&interface==WIFI_IF_AP&&!g_ic[20]&&!mode_creates&&!ap_starts);
 ++pmf_calls;if(fail_pmf)return -92;stored.ap.pmf_cfg.capable=stored.ap.pmf_cfg.required=false;return 0;
}
static bool matches(const wifi_config_t*a,const wifi_config_t*b){return !memcmp(a,b,sizeof(*a));}
