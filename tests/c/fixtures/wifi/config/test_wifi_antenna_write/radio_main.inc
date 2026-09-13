
int main(void){
 for(int bad=-1;bad<13;bad++){
  reset();memset(&s_radio,0,sizeof(s_radio));memset(&s_tx_rate_lease,0,sizeof(s_tx_rate_lease));
  s_radio.driver_owned=s_radio.storage_configured=true;s_radio.driver_state=ESP32_MQUICKJS_WIFI_RADIO_STOPPED;
  switch(bad){
   case 0:s_radio.driver_owned=false;break;case 1:s_radio.storage_configured=false;break;
   case 2:s_radio.started=true;break;case 3:s_radio.stop_required=true;break;
   case 4:s_radio.restart_required=true;break;case 5:s_radio.driver_state=ESP32_MQUICKJS_WIFI_RADIO_STARTED;break;
   case 6:s_radio.fault_stage="older";break;case 7:s_radio.cleanup_stage="cleanup";break;
   case 8:s_radio.lifecycle.identity=7;break;case 9:s_radio.operation.identity=8;break;
   case 10:s_radio.wake_locks=1;break;case 11:s_radio.leases[15].identity=99;break;
   case 12:s_tx_rate_lease.restore_pending=true;break;
  }
  esp32_mquickjs_wifi_antenna_snapshot_t request={0};request.config.enabled_ant1=15;
  esp32_mquickjs_wifi_radio_config_result_t r={0};int error=esp32_mquickjs_wifi_radio_write_antenna(false,&request,&r);
  assert(!radio_lock&&!lock_depth&&s_radio.configuration.error==error);
  if(bad<0)assert(!error&&writes==1);else assert(error&&!writes&&!reads);
 }
 reset();memset(&s_radio,0,sizeof(s_radio));memset(&s_tx_rate_lease,0,sizeof(s_tx_rate_lease));
 s_radio.driver_owned=s_radio.storage_configured=true;s_radio.driver_state=ESP32_MQUICKJS_WIFI_RADIO_STOPPED;
 esp32_mquickjs_wifi_antenna_snapshot_t request={0};request.config.enabled_ant1=15;fail_set=1;fail_rollback=2;
 esp32_mquickjs_wifi_radio_config_result_t r={0};
 assert(esp32_mquickjs_wifi_radio_write_antenna(false,&request,&r)==-62);
 assert(s_radio.restart_required&&!strcmp(s_radio.fault_stage,"antenna-device-restart-required"));
 assert(s_radio.configuration.error==-62&&s_radio.configuration.rollback_error==-63);reset();
}
