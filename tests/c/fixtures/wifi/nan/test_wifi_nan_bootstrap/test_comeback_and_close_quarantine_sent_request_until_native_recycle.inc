
int main(void){
 setup();esp32_mquickjs_wifi_nan_pairing_t*p=request(30000);tick();tick();
 notice(ESP32_MQUICKJS_NAN_BOOTSTRAP_RESPONSE,ESP32_MQUICKJS_NAN_BOOTSTRAP_COMEBACK,
  WIFI_NAN_BOOTSTRAP_PIN_CODE_DISPLAY,9);
 assert(p->status.closing&&p->status.error==ESP_ERR_NOT_SUPPORTED);tick();
 accepted(9);assert(!p->status.peer_accepted&&!p->status.retired&&!pair_starts&&s->pairing==p);
 recycle(true);assert(p->status.retired&&!s->pairing);
 esp32_mquickjs_wifi_nan_pairing_release(p);finish();
}
