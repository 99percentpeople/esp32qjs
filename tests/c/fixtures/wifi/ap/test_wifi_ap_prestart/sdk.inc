
bool current_task_is_wifi_task(void){return wifi_task&&!wrong_task;}
int __real_wifi_mode_set(int requested){
 assert(!critical);if(requested==WIFI_MODE_APSTA){
  assert(wifi_task&&mode==WIFI_MODE_STA&&setters==1&&reads==1&&!g_ic[20]);
  if(fail_create)return -93;++mode_creates;g_ic[20]=1;
 }else if(requested==WIFI_MODE_STA)g_ic[20]=0;
 return 0;
}
int esp32qjs_wifi_ap_prestart_set_config_native(wifi_config_t*config){
 assert(!critical&&wifi_task&&mode==WIFI_MODE_STA&&!g_ic[20]&&!mode_creates&&!ap_starts);++setters;
 assert(esp32_mquickjs_wifi_ap_prestart_release(9,41)==ESP_ERR_INVALID_STATE);
 stored=*config;stored.ap.pmf_cfg.capable=true;config->ap.ssid[0]='?';
 if(fail_set==3)return setters==1?-77:-88;
 return fail_set==setters?-77:0;
}
static int esp_wifi_set_mode(wifi_mode_t requested){
 assert(requested==WIFI_MODE_APSTA);if(dispatch_failure)return -91;
 wifi_task=true;int error=__wrap_wifi_mode_set(requested);
 if(!error){assert(stored.ap.ssid[0]=='n'&&reads==1);++ap_starts;mode=requested;}
 else {assert(!__wrap_wifi_mode_set(WIFI_MODE_STA));mode=WIFI_MODE_STA;}
 wifi_task=false;return error;
}
static void prepare(void){
 mode_creates=ap_starts=setters=reads=fail_set=pmf_calls=0;
 wifi_task=bad_read=wrong_task=dispatch_failure=fail_pmf=fail_create=false;mode=WIFI_MODE_STA;memset(g_ic,0,sizeof(g_ic));
 wifi_config_t requested={0},previous={0};requested.ap.ssid[0]='n';previous.ap.ssid[0]='o';
 requested.ap.pmf_cfg.capable=previous.ap.pmf_cfg.capable=true;
 stored=previous;
 assert(!esp32_mquickjs_wifi_ap_prestart_prepare(9,41,&requested,&previous,matches));
}
