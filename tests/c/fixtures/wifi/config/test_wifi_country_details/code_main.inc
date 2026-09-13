
int main(void){
 reset(true);assert(code()==0&&mutation.driver_accepted&&actual.cc[0]=='T');int total=calls;
 /* A syntactically valid but different SDK readback used to report success. */
 reset(true);corrupt_at=total;assert(code()==ESP_ERR_INVALID_RESPONSE&&mutation.driver_accepted);
 assert(s_radio.configuration.rollback_complete&&s_radio.configuration.persistent_mutation_possible);restored();
 for(int nth=1;nth<=total;nth++){
  reset(true);fail_at=nth;assert(code()==-77&&s_radio.configuration.error==-77);restored();
  if(s_radio.configuration.mutation_attempted)assert(s_radio.configuration.rollback_complete);
  else assert(!writes);
 }
 reset(true);fail_at=total;fail_second=total+1;assert(code()==-77);
 assert(s_radio.configuration.rollback_error==-88&&s_radio.fault_stage&&s_radio.cleanup_stage);
 for(int gate=0;gate<5;gate++){
  reset(true);if(gate==0)lease.generation--;if(gate==1)lease.identity++;
  if(gate==2){s_radio.leases[1].identity=8;s_radio.leases[1].client=ESP32_MQUICKJS_WIFI_RADIO_CLIENT_ESPNOW;}
  if(gate==3)s_radio.effective_mode=WIFI_MODE_APSTA;if(gate==4)associated=true;
  assert(code()==ESP_ERR_INVALID_STATE&&!writes);
 }
 reset(true);s_radio.leases[1].identity=8;s_radio.leases[1].client=ESP32_MQUICKJS_WIFI_RADIO_CLIENT_APPLICATION;
 s_radio.clients[ESP32_MQUICKJS_WIFI_RADIO_CLIENT_APPLICATION]=1;assert(code()==0&&writes==1);
}
