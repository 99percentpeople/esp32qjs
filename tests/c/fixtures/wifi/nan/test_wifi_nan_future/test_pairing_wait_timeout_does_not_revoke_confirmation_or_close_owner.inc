
int main(void){
 setup();esp32_mquickjs_wifi_nan_pairing_t*p=prepare(30000);
 esp32_mquickjs_future_driver_state_t state={.operation=NAN_PAIRING_READY,.pairing=p,.timeout_ms=10};
 assert(nan_start(NULL,NULL,0,&state));run_queued(NULL);
 assert(nan_on_timeout(NULL,&state,10)==-1&&wait_timeout_seen&&!p->status.closing);
 assert(nan_cancel_wait(&state)==ESP32_MQUICKJS_CANCELLED&&!p->status.closing);
 assert(!esp32_mquickjs_wifi_nan_pairing_confirm(p,true,123456));tick();assert(pair_starts==1);
 state=(esp32_mquickjs_future_driver_state_t){.operation=NAN_PAIRING_CLOSE,.pairing=p,.timeout_ms=10};
 pair_close_error=ESP_ERR_TIMEOUT;assert(nan_start(NULL,NULL,0,&state));run_queued(NULL);
 assert(p->status.closing&&!p->status.retired);
 assert(nan_on_timeout(NULL,&state,10)==-1&&!p->status.retired);
 pair_close_error=0;tick();assert(p->status.retired&&nan_poll(&state)==ESP32_MQUICKJS_FUTURE_READY);
 assert(nan_timeout(&state)==10);nan_destroy(NULL);
 esp32_mquickjs_wifi_nan_pairing_release(p);finish();
}
