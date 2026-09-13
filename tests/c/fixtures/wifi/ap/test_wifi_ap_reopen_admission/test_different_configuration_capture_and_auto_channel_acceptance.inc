
int main(void) {
 for(int failure=0;failure<2;failure++) {
  memset(&s_radio,0,sizeof(s_radio));captures=0;capture_error=failure?-91:0;
  s_radio.generation=9;s_radio.next_lease_identity=3;s_radio.next_lifecycle_identity=41;
  s_radio.driver_owned=s_radio.storage_configured=s_radio.started=true;
  s_radio.driver_state=ESP32_MQUICKJS_WIFI_RADIO_STARTED;
  s_radio.effective_mode=s_radio.event_live=WIFI_MODE_STA;
  esp32_mquickjs_wifi_radio_lease_t app={9,1,ESP32_MQUICKJS_WIFI_RADIO_CLIENT_APPLICATION,true};
  esp32_mquickjs_wifi_radio_lease_t sta={9,2,ESP32_MQUICKJS_WIFI_RADIO_CLIENT_WIFI_STA,true},ap={0};
  s_radio.clients[app.client]=s_radio.clients[sta.client]=1;
  s_radio.leases[0]=(wifi_radio_live_lease_t){.identity=1,.client=app.client,.required_mode=WIFI_MODE_STA};
  s_radio.leases[1]=(wifi_radio_live_lease_t){.identity=2,.client=sta.client,.required_mode=WIFI_MODE_STA};
  wifi_config_t requested={0};requested.ap.ssid_len=4;memcpy(requested.ap.ssid,"test",4);
  requested.ap.max_connection=4;requested.ap.beacon_interval=100;
  requested.ap.dtim_period=1;requested.ap.csa_count=3;
  requested.ap.pairwise_cipher=WIFI_CIPHER_TYPE_CCMP;requested.ap.sae_pwe_h2e=WPA3_SAE_PWE_UNSPECIFIED;
  stored=requested;stored.ap.ssid[0]='o';
  esp32_mquickjs_wifi_radio_lifecycle_t token={0};
  int error=esp32_mquickjs_wifi_radio_begin_ap_reopen(&app,&sta,&requested,&ap,&token);
  assert(captures==1&&!live_alloc&&!depth&&!critical&&error==capture_error);
  if(failure)assert(!token.identity&&!ap.acquired&&!s_radio.ap_reopen_config_pending);
  else assert(token.identity==41&&ap.identity==3&&s_radio.ap_reopen_config_pending);
  assert(app.identity==1&&sta.identity==2&&s_radio.effective_mode==WIFI_MODE_STA);
  wifi_config_t observed=requested;observed.ap.channel=6;
  assert(wifi_radio_ap_prestart_config_matches(&requested,&observed));
  assert(!wifi_radio_ap_reopen_config_matches(&requested,&observed));
  requested.ap.channel=1;assert(!wifi_radio_ap_prestart_config_matches(&requested,&observed));
  observed=requested;observed.ap.ssid[0]='x';assert(!wifi_radio_ap_prestart_config_matches(&requested,&observed));
 }
}
