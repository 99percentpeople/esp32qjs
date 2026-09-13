
static esp_err_t esp32qjs_pairing_start_native(wifi_nan_pairing_config_t*config){
 assert(wifi_task&&!locked);++native_calls;
 if(fail_native)return ESP_ERR_NO_MEM;
 snapshot=(esp32_mquickjs_wifi_nan_pasn_status_t){.identity=++last_identity,.active=true};
 memcpy(snapshot.peer,config->peer_nmi,6);
 uint8_t own=0,remote=0;
 assert(esp32_mquickjs_wifi_nan_pairing_services(config->peer_nmi,&own,&remote)&&own==2&&remote==9);
 if(config->pairing_verification){uint8_t npk[32];size_t len=0;int akmp=0;
  assert(!nan_global_peer_npk_lookup(npk,&len,&akmp)&&len==32&&akmp==7&&npk[0]==0x62);
 }
 return ESP_OK;
}
static void release(void){wifi_task=true;snapshot.closing=true;esp32_mquickjs_wifi_nan_pairing_release_binding();wifi_task=false;}
