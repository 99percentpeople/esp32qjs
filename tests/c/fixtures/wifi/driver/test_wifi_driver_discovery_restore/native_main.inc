
int main(void){
 esp32_mquickjs_wifi_radio_config_result_t r;
 reset();assert(esp32_mquickjs_wifi_radio_restore(&r)==0&&calls==3);
 assert(r.mutation_attempted&&r.persistent_mutation_possible&&!r.rollback_attempted);
 assert(!strcmp(r.stage,"restore-sdk-accepted")&&s_radio.generation==9&&s_radio.driver_owned);
 assert(s_radio.effective_mode==EXPECTED_MODE&&!s_radio.started&&s_radio.storage==WIFI_STORAGE_FLASH);
 assert(invalidated==3&&!unchanged&&!s_inactive_history&&!s_radio.primary_channel);
 for(int phase=1;phase<=3;phase++){
  reset();fail_at=phase;assert(esp32_mquickjs_wifi_radio_restore(&r)==-77);
  assert(calls==phase&&invalidated==3&&!unchanged&&!r.rollback_attempted);
  assert(s_radio.fault_error==-77&&r.error==-77&&s_radio.configuration.error==-77);
  assert(phase!=3||!s_radio.storage_configured);
  const char *first=s_radio.fault_stage;int previous=calls;unsigned channel_generation=s_radio.channel_generation;
  assert(s_radio.driver_state==ESP32_MQUICKJS_WIFI_RADIO_CLEANUP_PENDING);
  /* A rejected call may replace configuration, but cannot lose native progress. */
  s_radio.leases[0].identity=5;
  assert(esp32_mquickjs_wifi_radio_restore(&r)==ESP_ERR_INVALID_STATE&&calls==previous);
  assert(s_radio.fault_stage==first&&s_radio.cleanup_stage==first);
  s_radio.leases[0].identity=0;
  /* Repeated SDK failure keeps the first error and retries only the suffix. */
  fail_at=calls+1;assert(esp32_mquickjs_wifi_radio_restore(&r)==-77&&calls==previous+1);
  assert(s_radio.fault_stage==first&&s_radio.fault_error==-77);
  assert(r.mutation_attempted==(phase!=2)&&r.persistent_mutation_possible==(phase==1));
  previous=calls;fail_at=0;assert(esp32_mquickjs_wifi_radio_restore(&r)==ESP_OK&&calls==previous+4-phase);
  assert(!s_radio.fault_stage&&!s_radio.cleanup_stage&&!s_radio.fault_error&&!s_radio.cleanup_error);
  assert(s_radio.driver_state==ESP32_MQUICKJS_WIFI_RADIO_STOPPED&&s_radio.storage_configured);
  assert(s_radio.generation==9&&s_radio.effective_mode==EXPECTED_MODE);
  assert(invalidated==(phase==1?9:3));
  assert(s_radio.channel_generation==channel_generation+(phase==1?2:0));
 }
 const char *allowed[]={"mode-snapshot","mode-write","mode-readback","station-phy-config",
  "ap-phy-config","station-phy-readback","ap-phy-readback"};
 for(unsigned origin=0;origin<sizeof(allowed)/sizeof(*allowed);++origin){
  for(int rollback=0;rollback<2;++rollback){
   reset();s_radio.fault_stage=allowed[origin];s_radio.fault_error=-88;
   s_radio.driver_state=ESP32_MQUICKJS_WIFI_RADIO_FAULTED;
   if(rollback){s_radio.cleanup_stage=origin<3?"mode-rollback":"phy-rollback";
    s_radio.cleanup_error=-99;s_radio.driver_state=ESP32_MQUICKJS_WIFI_RADIO_CLEANUP_PENDING;}
   fail_at=2;assert(esp32_mquickjs_wifi_radio_restore(&r)==-77&&calls==2);
   assert(s_radio.fault_stage==allowed[origin]&&s_radio.fault_error==-88&&s_radio.cleanup_error==-77);
   assert(!strcmp(s_radio.cleanup_stage,"restore-mode-readback"));
   fail_at=0;assert(esp32_mquickjs_wifi_radio_restore(&r)==ESP_OK&&calls==4&&invalidated==3);
   assert(!s_radio.fault_stage&&!s_radio.cleanup_stage&&s_radio.driver_state==ESP32_MQUICKJS_WIFI_RADIO_STOPPED);
  }
 }
 const char *denied[]={"antenna-device-restart-required","configuration-rollback","country-config",
  "power-save-readback","event-mask-write","band-write","he-statistics-readback","init"};
 for(unsigned origin=0;origin<sizeof(denied)/sizeof(*denied);++origin){
  reset();s_radio.fault_stage=denied[origin];s_radio.cleanup_stage="restore-storage";
  s_radio.driver_state=ESP32_MQUICKJS_WIFI_RADIO_CLEANUP_PENDING;
  assert(esp32_mquickjs_wifi_radio_restore(&r)==ESP_ERR_INVALID_STATE&&!calls);
 }
 reset();s_radio.fault_stage="mode-write";s_radio.cleanup_stage="configuration-rollback";
 s_radio.driver_state=ESP32_MQUICKJS_WIFI_RADIO_CLEANUP_PENDING;
 assert(esp32_mquickjs_wifi_radio_restore(&r)==ESP_ERR_INVALID_STATE&&!calls);
 reset();s_radio.storage=WIFI_STORAGE_RAM;
 assert(esp32_mquickjs_wifi_radio_restore(&r)==ESP_OK&&calls==3&&r.persistent_mutation_possible);
 reset();native_mode=(wifi_mode_t)99;assert(esp32_mquickjs_wifi_radio_restore(&r)==ESP_ERR_INVALID_RESPONSE);
 assert(calls==2&&s_radio.effective_mode==WIFI_MODE_STA);
#if !CONFIG_ESP_WIFI_SOFTAP_SUPPORT
 reset();native_mode=WIFI_MODE_AP;
 assert(esp32_mquickjs_wifi_radio_restore(&r)==ESP_ERR_NOT_SUPPORTED&&calls==2);
#endif
 for(unsigned bit=1;bit<=32;bit<<=1){reset();pending=bit;assert(esp32_mquickjs_wifi_radio_restore(&r)==ESP_ERR_INVALID_STATE&&calls==0&&unchanged);}
 for(int recovery=0;recovery<2;++recovery){for(int gate=0;gate<24;gate++){
  reset();if(recovery){s_radio.fault_stage="mode-write";s_radio.driver_state=ESP32_MQUICKJS_WIFI_RADIO_FAULTED;}
  switch(gate){
   case 0:s_radio.started=true;break;case 1:s_radio.stop_required=true;break;
   case 2:s_radio.stop_submitted=true;break;case 3:s_radio.leases[1].identity=99;break;
   case 4:s_radio.operation.identity=3;break;case 5:s_radio.lifecycle.identity=4;break;
   case 6:s_radio.event_phase=1;break;case 7:s_radio.event_live=1;break;
   case 8:s_radio.wake_locks=1;break;case 9:s_radio.promiscuous_claimed=true;break;
   case 10:s_tx_rate_lease.restore_pending=true;break;case 11:s_interval.restore_pending=true;break;
   case 12:s_config_restart.snapshot=&r;break;case 13:s_policy_restart.owner.identity=2;break;
   case 14:s_vendor_ie.start_owner.identity=1;break;case 15:s_radio.fault_stage="prior";break;
   case 16:s_radio.cleanup_stage="retiring";break;case 17:s_radio.restart_required=true;break;
   case 18:s_tx_rate_lease.identity=5;break;case 19:s_interval.owner.identity=6;break;
   case 20:s_radio.storage_configured=false;break;case 21:s_radio.storage=(wifi_storage_t)99;break;
   case 22:s_radio.driver_state=ESP32_MQUICKJS_WIFI_RADIO_UNINITIALIZED;break;
   case 23:s_radio.driver_state=ESP32_MQUICKJS_WIFI_RADIO_STARTED;break;
  }
  assert(esp32_mquickjs_wifi_radio_restore(&r)==ESP_ERR_INVALID_STATE&&calls==0&&!r.mutation_attempted&&unchanged);
 }}
 reset();s_radio.driver_owned=false;assert(esp32_mquickjs_wifi_radio_restore(&r)==ESP_ERR_WIFI_NOT_INIT&&calls==0);
 assert(esp32_mquickjs_wifi_radio_restore(NULL)==ESP_ERR_INVALID_ARG&&!depth&&!critical);
 return 0;
}
