
int main(void){
 setup();uint8_t mac[6]={2,3,4,5,6,7};
 for(unsigned i=0;i<ESP32_MQUICKJS_NAN_BOOTSTRAP_PEERS;i++){
  esp32_mquickjs_wifi_nan_pairing_t*p=NULL;mac[5]=(uint8_t)(7+i);
  assert(!esp32_mquickjs_wifi_nan_pairing_create(d,9,mac,true,1000,&p));
  assert(!esp32_mquickjs_wifi_nan_pairing_request(p));assert(!esp32_mquickjs_wifi_nan_pairing_activate(p));
  esp32_mquickjs_wifi_nan_pairing_close(p,false);tick();assert(p->status.retired);
  esp32_mquickjs_wifi_nan_pairing_release(p);
 }
 esp32_mquickjs_wifi_nan_pairing_t*p=NULL;mac[5]=99;
 assert(!esp32_mquickjs_wifi_nan_pairing_create(d,9,mac,true,1000,&p));
 assert(!esp32_mquickjs_wifi_nan_pairing_request(p));
 assert(esp32_mquickjs_wifi_nan_pairing_activate(p)==ESP_ERR_NO_MEM);
 assert(!p->status.activated&&!pair_starts&&!bootstrap_sends);
 esp32_mquickjs_wifi_nan_pairing_release(p);finish();
}
