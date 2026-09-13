
int main(void){
 setup();esp32_mquickjs_wifi_nan_pairing_t*p=prepare(30000);
 assert(!esp32_mquickjs_wifi_nan_pairing_confirm(p,true,123456));tick();
 pair_close_error=ESP_ERR_TIMEOUT;close_error=ESP_ERR_TIMEOUT;
 assert(!esp32_mquickjs_wifi_nan_prepare_runtime_destroy());run_queued(NULL);
 assert(s->status.closing&&!s->status.retired&&!p->status.retired&&closes==1);
 close_error=0;tick();assert(s->status.retired&&p->status.retired&&!s->pairing);
 esp32_mquickjs_wifi_nan_pairing_release(p);finish();
}
