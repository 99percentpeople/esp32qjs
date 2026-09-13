
static void prepare(void){reset();s_radio.lifecycle.identity=0;memset(&s_tx_rate_lease,0,sizeof(s_tx_rate_lease));}
static int write_config(void){return esp32_mquickjs_wifi_radio_write_interface_config(WIFI_IF_STA,&requested[0],&result);}
int main(void){
 prepare();assert(write_config()==0);int total=calls;assert(!memcmp(&native_config[0],&requested[0],sizeof(wifi_config_t)));
 assert(native_mode==WIFI_MODE_STA&&native_storage==WIFI_STORAGE_RAM&&!s_radio.started&&!alloc_live);
 for(int nth=1;nth<=total;nth++){
  prepare();fail_at=nth;assert(write_config()==-77&&result.error==-77);
  assert(!alloc_live&&!depth&&!critical&&native_mode==WIFI_MODE_STA&&native_storage==WIFI_STORAGE_RAM);
  assert(!memcmp(native_config,before,sizeof(before)));
  if(result.mutation_attempted)assert(result.rollback_complete);else assert(!writes);
 }
 prepare();fail_at=total;rollback_fail=1;assert(write_config()==-77&&result.rollback_error==-88&&s_radio.fault_stage&&s_radio.cleanup_stage);
 for(int gate=0;gate<7;gate++){
  prepare();if(gate==0)s_radio.leases[1].identity=1;if(gate==1)s_radio.lifecycle.identity=1;
  if(gate==2)s_radio.operation.identity=1;if(gate==3)s_tx_rate_lease.restore_pending=true;
  if(gate==4)s_radio.wake_locks=1;if(gate==5)s_radio.started=true;if(gate==6)s_radio.effective_mode=WIFI_MODE_AP;
  assert(write_config()==ESP_ERR_INVALID_STATE&&!calls&&!alloc_live);
 }
 prepare();allocation_failure=true;assert(write_config()==ESP_ERR_NO_MEM&&!calls&&!alloc_live);
}
