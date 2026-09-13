
int main(void){
 const char *stage;wifi_config_t config;
 s_radio.driver_owned=s_radio.storage_configured=true;s_radio.driver_state=ESP32_MQUICKJS_WIFI_RADIO_STOPPED;
 for(int interface=0;interface<2;interface++){
  assert(esp32_mquickjs_wifi_radio_read_interface_config(interface,false,&config,&stage)==0&&!stage);
  const uint8_t *secret=interface==WIFI_IF_STA?config.sta.password:config.ap.password;
  for(unsigned i=0;i<64;i++)assert(secret[i]==0);
  if(interface==WIFI_IF_STA)for(unsigned i=0;i<sizeof(config.sta.sae_h2e_identifier);i++)assert(!config.sta.sae_h2e_identifier[i]);
  int before=reads;memset(&config,0x55,sizeof(config));
  int err=esp32_mquickjs_wifi_radio_read_interface_config(interface,true,&config,&stage);
#if CONFIG_ESP32_MQUICKJS_WIFI_ALLOW_SECRET_READBACK
  assert(err==0&&reads==before+1&&secret[0]);
#else
  assert(err==ESP_ERR_NOT_ALLOWED&&reads==before&&!strcmp(stage,"secret-readback"));
  for(unsigned i=0;i<sizeof(config);i++)assert(!((uint8_t*)&config)[i]);
#endif
 }
 native_error=-77;assert(esp32_mquickjs_wifi_radio_read_interface_config(WIFI_IF_STA,false,&config,&stage)==-77);
 for(unsigned i=0;i<sizeof(config);i++)assert(!((uint8_t*)&config)[i]);native_error=0;
 for(int gate=0;gate<6;gate++){
  s_radio.lifecycle.identity=s_radio.operation.identity=0;s_radio.restart_required=false;s_radio.fault_stage=s_radio.cleanup_stage=NULL;s_radio.driver_owned=true;
  if(gate==0)s_radio.lifecycle.identity=1;if(gate==1)s_radio.operation.identity=1;if(gate==2)s_radio.restart_required=true;
  if(gate==3)s_radio.fault_stage="fault";if(gate==4)s_radio.cleanup_stage="cleanup";if(gate==5)s_radio.driver_owned=false;
  int count=reads;assert(esp32_mquickjs_wifi_radio_read_interface_config(WIFI_IF_STA,false,&config,&stage)!=0&&reads==count);
 }
 assert(!depth&&!critical);
}
