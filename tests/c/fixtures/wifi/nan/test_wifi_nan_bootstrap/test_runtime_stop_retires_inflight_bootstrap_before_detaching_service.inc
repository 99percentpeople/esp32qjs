
int main(void){
 setup();esp32_mquickjs_wifi_nan_pairing_t*p=request(30000);tick();tick();
 close_error=ESP_ERR_TIMEOUT;assert(!esp32_mquickjs_wifi_nan_prepare_runtime_destroy());run_queued(NULL);
 assert(s->status.closing&&!s->status.retired&&!p->status.retired&&s->message);
 close_error=0;tick();assert(s->status.retired&&p->status.retired&&!s->pairing&&!s->message&&!d->parent);
 assert(!p->bootstrap_message&&!p->status.bootstrap_tx_pending&&!pair_starts);
 esp32_mquickjs_wifi_nan_pairing_release(p);finish();
}
