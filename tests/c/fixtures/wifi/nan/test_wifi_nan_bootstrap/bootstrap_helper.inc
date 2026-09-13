
static void notice(uint8_t type,uint8_t status,uint16_t methods,uint8_t peer_id){
 esp32_mquickjs_wifi_nan_sdk_bootstrap_t data={.type=type,.status=status,.methods=methods,.peer_service_id=peer_id};
 esp32_mquickjs_wifi_nan_sdk_notice_t n={.kind=ESP32_MQUICKJS_NAN_SDK_BOOTSTRAP,
  .service_id=7,.peer={2,3,4,5,6,7},.data=&data,.size=sizeof(data)};
 native_observer(begins,&n,native_opaque);
}
static __attribute__((unused)) void incoming(void){
 notice(ESP32_MQUICKJS_NAN_BOOTSTRAP_REQUEST,ESP32_MQUICKJS_NAN_BOOTSTRAP_ACCEPTED,
  WIFI_NAN_BOOTSTRAP_PIN_CODE_KEYPAD,9);
}
static __attribute__((unused)) void accepted(uint8_t peer){
 notice(ESP32_MQUICKJS_NAN_BOOTSTRAP_RESPONSE,ESP32_MQUICKJS_NAN_BOOTSTRAP_ACCEPTED,
  WIFI_NAN_BOOTSTRAP_PIN_CODE_DISPLAY,peer);
}
static __attribute__((unused)) void recycle(bool success){
 assert(message_tx.identity);message_tx.tx_done=true;message_tx.tx_succeeded=success;
 message_tx.buffer_retired=true;message_tx.completed_us=message_tx.retired_us=clock_us;tick();
}
static __attribute__((unused)) esp32_mquickjs_wifi_nan_pairing_t*request(uint32_t timeout){
 uint8_t mac[6]={2,3,4,5,6,7};esp32_mquickjs_wifi_nan_pairing_t*p=NULL;
 assert(!esp32_mquickjs_wifi_nan_pairing_create(d,9,mac,true,timeout,&p));
 assert(!esp32_mquickjs_wifi_nan_pairing_request(p));
 assert(!esp32_mquickjs_wifi_nan_pairing_activate(p));return p;
}
