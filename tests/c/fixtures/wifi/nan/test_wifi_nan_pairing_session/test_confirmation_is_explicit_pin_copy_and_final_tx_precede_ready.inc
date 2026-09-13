
int main(void){
 setup();esp32_mquickjs_wifi_nan_pairing_t*p=prepare(30000);tick();
 assert(!pair_starts&&!p->status.confirmed&&!p->pin);
 assert(!esp32_mquickjs_wifi_nan_pairing_confirm(p,true,1234));
 assert(esp32_mquickjs_wifi_nan_pairing_confirm(p,true,999999)==ESP_ERR_INVALID_STATE);
 tick();assert(pair_starts==1&&seen_pin==1234&&!p->pin&&!p->status.ready);
 pair_native.authenticated=pair_native.paired=true;pair_native.traffic_pending=true;
 pair_native.completed_us=clock_us;tick();assert(!p->status.ready);
 pair_native.traffic_pending=false;pair_native.completed_us=clock_us;tick();assert(p->status.ready);
 pair_close_error=ESP_ERR_TIMEOUT;esp32_mquickjs_wifi_nan_pairing_close(p,false);tick();
 assert(!p->status.retired&&s->pairing==p&&!s->status.closing);
 esp32_mquickjs_wifi_nan_pairing_t*other=NULL;uint8_t mac[6]={2,3,4,5,6,7};
 assert(esp32_mquickjs_wifi_nan_pairing_create(d,9,mac,true,1000,&other)==ESP_ERR_INVALID_STATE&&!other);
 pair_close_error=0;tick();assert(p->status.retired&&!s->pairing&&pair_starts==1);
 unsigned old_closes=pair_closes;esp32_mquickjs_wifi_nan_pairing_close(p,false);
 esp32_mquickjs_wifi_nan_pairing_release(p);assert(pair_closes==old_closes);finish();
}
