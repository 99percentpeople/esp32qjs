
int main(void){
 esp32_mquickjs_wifi_configuration_execution_t result;
 stopped_setup(WIFI_MODE_NULL);history_missing=true;
 assert(!esp32_mquickjs_wifi_restart_stopped_interfaces(false,&result));
 assert(result.admitted&&result.configuration_attempted&&result.resume_attempted&&running);
 assert(target_mode==WIFI_MODE_STA&&reconstructed&&replayed&&!phase_calls[10]&&!phase_calls[11]);
 assert(phase_calls[1]==1&&phase_calls[2]==1&&phase_calls[3]==1&&phase_calls[9]==1);
 assert(!s_wifi_lifecycle.identity&&!s_wifi_state.runtime_cleanup_pending&&!temporary);
 for(unsigned failure=1;failure<=9;++failure){
  if(failure>=4&&failure!=5&&failure!=9)continue;
  stopped_setup(WIFI_MODE_NULL);history_missing=true;phase_failure=failure;
  assert(esp32_mquickjs_wifi_restart_stopped_interfaces(false,&result)==-(100+(int)failure));
  assert(result.admitted&&s_wifi_lifecycle.identity==41&&s_wifi_configuration_cleanup);
  assert(!phase_calls[10]&&!phase_calls[11]&&!temporary);
  if(failure==1) {
   unsigned previous[13];memcpy(previous,phase_calls,sizeof(previous));
   assert(esp32_mquickjs_wifi_restart_stopped_interfaces(false,&result)==ESP_ERR_INVALID_STATE);
   assert(!memcmp(previous,phase_calls,sizeof(previous)));
  }
  phase_failure=0;assert(!wifi_finish_configuration_cleanup());
  assert(!s_wifi_lifecycle.identity&&!s_wifi_state.runtime_cleanup_pending&&!running&&!sta_storage);
 }
}
