
static wifi_country_t requested,actual,before;
static esp32_mquickjs_wifi_radio_config_result_t result;
static esp32_mquickjs_wifi_radio_lease_t lease;
static esp32_mquickjs_wifi_radio_mutation_t mutation;
static void reset(bool live){
 assert(!depth&&!critical);memset(&s_radio,0,sizeof(s_radio));memset(&s_tx_rate_lease,0,sizeof(s_tx_rate_lease));
 memset(&result,0,sizeof(result));memset(&actual,0,sizeof(actual));calls=writes=fail_at=fail_second=corrupt_at=0;associated=false;
 s_radio.driver_owned=s_radio.storage_configured=true;s_radio.generation=9;s_radio.effective_mode=WIFI_MODE_STA;
 s_radio.started=live;s_radio.driver_state=live?ESP32_MQUICKJS_WIFI_RADIO_STARTED:ESP32_MQUICKJS_WIFI_RADIO_STOPPED;
 native_country=(wifi_country_t){.cc="US",.schan=1,.nchan=11,.max_tx_power=20,.policy=WIFI_COUNTRY_POLICY_MANUAL};before=native_country;
 requested=(wifi_country_t){.cc={'T','W','X'},.schan=1,.nchan=13,.policy=WIFI_COUNTRY_POLICY_MANUAL};
 lease=(esp32_mquickjs_wifi_radio_lease_t){.generation=9,.identity=7,.client=ESP32_MQUICKJS_WIFI_RADIO_CLIENT_WIFI_STA,.acquired=true};
 if(live){s_radio.leases[0].identity=7;s_radio.leases[0].client=lease.client;s_radio.clients[lease.client]=1;}
}
static int details(void){return esp32_mquickjs_wifi_radio_set_country_details(&requested,&actual,&result);}
static int code(void){return esp32_mquickjs_wifi_radio_set_country_code(&lease,"TW",false,&actual,&mutation);}
static void restored(void){assert(wifi_radio_country_equal(&before,&native_country,true));assert(!depth&&!critical);}
