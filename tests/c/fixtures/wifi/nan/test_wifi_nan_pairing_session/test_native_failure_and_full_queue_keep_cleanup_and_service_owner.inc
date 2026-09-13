
int main(void){
 setup();q.full=true;esp32_mquickjs_wifi_nan_pairing_t*p=prepare(30000);
 assert(!esp32_mquickjs_wifi_nan_pairing_confirm(p,true,123456));
 queue_full=true;assert(!esp32_mquickjs_wifi_nan_service()&&p->pin==123456&&!pair_starts);
 queue_full=false;pair_start_error=-73;pair_close_error=ESP_ERR_TIMEOUT;tick();
 assert(p->status.error==-73&&p->status.closing&&!p->pin);
 esp32_mquickjs_wifi_nan_discovery_close(d);tick();
 assert(!d->status.retired&&!service_cancels&&!p->status.retired);
 pair_close_error=0;tick();assert(p->status.retired&&d->status.retired&&service_cancels==1);
 esp32_mquickjs_wifi_nan_pairing_release(p);finish();
}
