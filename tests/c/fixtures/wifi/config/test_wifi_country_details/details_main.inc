
int main(void){
 for(int mode=WIFI_MODE_NULL;mode<=WIFI_MODE_APSTA;mode++){
  reset(false);s_radio.effective_mode=mode;assert(details()==0);assert(actual.max_tx_power==20&&actual.cc[2]=='X');
  assert(writes==1&&result.persistent_mutation_possible&&!result.rollback_attempted);
 }
 reset(false);requested=before;requested.max_tx_power=0;assert(details()==0&&!writes&&calls==1);
 reset(false);assert(details()==0);int total=calls;
 for(int nth=1;nth<=total;nth++){
  reset(false);fail_at=nth;assert(details()==-77&&result.error==-77);restored();
  assert(result.rollback_attempted==(nth>1));assert(result.persistent_mutation_possible==(nth>1));
  if(nth>1)assert(result.rollback_complete);
 }
 for(int offset=1;offset<=2;offset++){
  reset(false);fail_at=total;fail_second=total+offset;assert(details()==-77);
  assert(result.rollback_error==-88&&!result.rollback_complete&&s_radio.fault_stage&&s_radio.cleanup_stage);
  int count=calls;fail_at=fail_second=0;assert(details()==ESP_ERR_INVALID_STATE&&calls==count);
 }
 reset(false);corrupt_at=3;assert(details()==ESP_ERR_INVALID_RESPONSE&&result.rollback_complete);restored();
 for(int gate=0;gate<12;gate++){
  reset(false);
  switch(gate){case 0:s_radio.leases[1].identity=44;break;case 1:s_radio.started=true;break;
   case 2:s_radio.lifecycle.identity=1;break;case 3:s_radio.operation.identity=1;break;
   case 4:s_radio.wake_locks=1;break;case 5:s_radio.promiscuous_claimed=true;break;
   case 6:s_radio.stop_required=true;break;case 7:s_radio.restart_required=true;break;
   case 8:s_radio.storage_configured=false;break;case 9:s_tx_rate_lease.restore_pending=true;break;
   case 10:s_radio.cleanup_stage="pending";break;case 11:s_radio.fault_stage="fault";break;}
  assert(details()==ESP_ERR_INVALID_STATE&&!calls);
 }
 reset(false);requested.nchan=15;assert(details()==ESP_ERR_INVALID_ARG&&!calls);
 reset(false);requested.max_tx_power=20;assert(details()==ESP_ERR_INVALID_ARG&&!calls);
 reset(false);s_radio.driver_owned=false;assert(details()==ESP_ERR_WIFI_NOT_INIT&&!calls);
 reset(false);wifi_country_t copy=requested;
 assert(esp32_mquickjs_wifi_radio_set_country_details(&requested,&requested,&result)==ESP_ERR_INVALID_ARG);
 assert(!memcmp(&copy,&requested,sizeof(copy))&&!calls);
}
