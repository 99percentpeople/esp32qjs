
int main(void){
 setup();esp32_mquickjs_wifi_nan_pairing_t*p=prepare(1000);
 assert(!esp32_mquickjs_wifi_nan_pairing_confirm(p,false,0));tick();
 assert(p->status.rejected&&p->status.retired&&!pair_starts&&!p->pin);
 esp32_mquickjs_wifi_nan_pairing_release(p);p=prepare(1000);
 clock_us+=1000000;tick();assert(p->status.timed_out&&p->status.retired&&!pair_starts);
 assert(esp32_mquickjs_wifi_nan_pairing_confirm(p,true,123456)==ESP_ERR_INVALID_STATE);
 esp32_mquickjs_wifi_nan_pairing_release(p);finish();
}
