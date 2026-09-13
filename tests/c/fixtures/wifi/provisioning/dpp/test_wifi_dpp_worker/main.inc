
static dpp_ipc_config_t pending;
int esp_wifi_ipc_internal(dpp_ipc_config_t*config,bool sync){
 assert(sync&&!config->arg_size&&config->arg==allocation);
 if(ipc_mode==1)return ESP_ERR_NO_MEM;
 if(ipc_mode==2){pending=*config;return 909;}
 return config->fn(config->arg);
}
static void zero_result(esp32_mquickjs_wifi_dpp_worker_t*w){
 for(size_t i=0;i<sizeof(w->result);i++)assert(!((uint8_t*)&w->result)[i]);
}
int main(void){
 esp32_mquickjs_wifi_dpp_worker_options_t options={.channels="1,6,11",.private_key_hex_der="abcd",.info="label",.has_key=true,.has_info=true};
 esp32_mquickjs_wifi_dpp_worker_t*w=NULL;
 validate_error=55;assert(esp32_mquickjs_wifi_dpp_worker_create(&options,&w)==55&&!w&&!allocations&&!begins);validate_error=0;
 fail_alloc=true;assert(esp32_mquickjs_wifi_dpp_worker_create(&options,&w)==ESP_ERR_NO_MEM&&!w);fail_alloc=false;
 assert(!esp32_mquickjs_wifi_dpp_worker_create(&options,&w));assert(!esp32_mquickjs_wifi_dpp_worker_prepare(w)&&begins==1&&bootstraps==1);
 for(size_t i=0;i<sizeof(w->options);i++)assert(!((uint8_t*)&w->options)[i]);
 assert(esp32_mquickjs_wifi_dpp_worker_listen(w)==ESP_ERR_NOT_FINISHED&&!listens);
 native.uri_length=9;native.uri_available=true;assert(!esp32_mquickjs_wifi_dpp_worker_listen(w)&&listens==1);
 assert(!esp32_mquickjs_wifi_dpp_worker_listen(w)&&listens==1);
 char uri[12];assert(esp32_mquickjs_wifi_dpp_worker_uri(w,uri,2,false)==ESP_ERR_INVALID_SIZE&&native.uri_available);zero_result(w);
 assert(!esp32_mquickjs_wifi_dpp_worker_uri(w,uri,sizeof(uri),false)&&!strcmp(uri,"DPP:K:x;;"));zero_result(w);
 assert(!esp32_mquickjs_wifi_dpp_worker_uri(w,NULL,0,true)&&!native.uri_available);
 native.terminal=true;native.configs_available=true;native.config_count=2;
 esp_dpp_config_data_t config;assert(!esp32_mquickjs_wifi_dpp_worker_config(w,1,&config)&&config.secret[0]==71);zero_result(w);
 assert(esp32_mquickjs_wifi_dpp_worker_config(w,2,&config)==ESP_ERR_INVALID_STATE);zero_result(w);
 assert(!esp32_mquickjs_wifi_dpp_worker_configs_commit(w)&&!native.configs_available);
 close_error=66;assert(esp32_mquickjs_wifi_dpp_worker_finish_capture(w)==66&&!w->capture_retired);
 close_error=0;assert(!esp32_mquickjs_wifi_dpp_worker_finish_capture(w)&&w->capture_retired);
 assert(!esp32_mquickjs_wifi_dpp_worker_config(w,0,&config));
 unsigned before=closes;release_error=77;assert(esp32_mquickjs_wifi_dpp_worker_close(w)==77&&closes==before&&!w->retired);
 release_error=0;assert(!esp32_mquickjs_wifi_dpp_worker_close(w)&&closes==before&&w->retired);
 assert(!esp32_mquickjs_wifi_dpp_worker_release(&w)&&!w&&!allocations);
 /* Independent selection begins only after capture and old result release. */
 for(unsigned failure=0;failure<4;failure++){
  assert(!esp32_mquickjs_wifi_dpp_worker_create(&options,&w));assert(!esp32_mquickjs_wifi_dpp_worker_prepare(w));
  native.terminal=true;assert(!esp32_mquickjs_wifi_dpp_worker_finish_capture(w));
  wifi_config_t station;esp32_mquickjs_wifi_dpp_auth_t chosen;memset(&config,71,sizeof(config));
  if(failure==1)release_error=77;if(failure==2)init_error=88;if(failure==3)select_error=99;
  unsigned old_begins=begins,old_selects=selects;
  int e=esp32_mquickjs_wifi_dpp_worker_select(w,&config,0,&chosen,&station);
  assert(e==(failure==1?77:failure==2?88:failure==3?99:0));assert(w->selection_attempted);
  assert(begins==old_begins+(failure!=1));assert(selects==old_selects+(failure==0||failure==3));
  if(!failure){assert(w->connection_selected&&station.secret[0]==42&&chosen==ESP32_MQUICKJS_DPP_AUTH_CONNECTOR);zero_result(w);}
  else for(size_t i=0;i<sizeof(station);i++)assert(!((uint8_t*)&station)[i]);
  release_error=init_error=select_error=0;
  assert(esp32_mquickjs_wifi_dpp_worker_select(w,&config,0,&chosen,&station)==ESP_ERR_INVALID_STATE);
  assert(!esp32_mquickjs_wifi_dpp_worker_close(w));assert(!esp32_mquickjs_wifi_dpp_worker_release(&w));
 }
 /* A failed initialization can own native storage and must close it. */
 assert(!esp32_mquickjs_wifi_dpp_worker_create(&options,&w));init_error=88;
 assert(esp32_mquickjs_wifi_dpp_worker_prepare(w)==88&&w->identity==41);init_error=0;
 assert(!esp32_mquickjs_wifi_dpp_worker_close(w));assert(!esp32_mquickjs_wifi_dpp_worker_release(&w));
 /* Definite pre-dispatch failure never reserved a native identity. */
 assert(!esp32_mquickjs_wifi_dpp_worker_create(&options,&w));ipc_mode=1;before=begins;
 assert(esp32_mquickjs_wifi_dpp_worker_prepare(w)==ESP_ERR_NO_MEM&&begins==before&&!w->identity);ipc_mode=0;
 assert(!esp32_mquickjs_wifi_dpp_worker_close(w));assert(!esp32_mquickjs_wifi_dpp_worker_release(&w));
 /* Unconfirmed dispatch retains every argument/result; a late receipt is
  * diagnostic evidence, not permission to repeat commands or free storage. */
 assert(!esp32_mquickjs_wifi_dpp_worker_create(&options,&w));ipc_mode=2;
 assert(esp32_mquickjs_wifi_dpp_worker_prepare(w)==909&&w->handoff_unknown);
 pending.fn(pending.arg);esp32_mquickjs_wifi_dpp_worker_status_t status;
 assert(!esp32_mquickjs_wifi_dpp_worker_status(w,&status)&&status.handoff_unknown&&!status.native.identity);
 assert(esp32_mquickjs_wifi_dpp_worker_close(w)==ESP_ERR_INVALID_STATE);
 assert(esp32_mquickjs_wifi_dpp_worker_release(&w)==ESP_ERR_INVALID_STATE&&allocations==1);
 return 0;
}
