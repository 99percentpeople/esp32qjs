
int main(void){
 setup();
 esp32_mquickjs_future_driver_state_t*state=calloc(1,sizeof(*state));assert(state);++allocations;
 assert(esp32_mquickjs_wifi_nan_discovery_retain(d));
 state->operation=NAN_PAIRING_RECEIVE;state->discovery=d;state->timeout_ms=10;
 assert(nan_start(NULL,NULL,0,state)&&!queued);
 assert(nan_timeout(state)==10&&nan_poll(state)==ESP32_MQUICKJS_FUTURE_PENDING);
 assert(nan_on_timeout(NULL,state,10)==JS_NULL&&!d->status.closing);
 incoming();tick();assert(nan_poll(state)==ESP32_MQUICKJS_FUTURE_READY&&state->pairing);
 uint32_t identity=state->pairing->status.identity;
 assert(nan_cancel_wait(state)==ESP32_MQUICKJS_CANCELLED);nan_destroy(state);
 assert(s->pairing&&!s->pairing->receiving&&!s->pairing->status.closing);
 esp32_mquickjs_wifi_nan_pairing_t*p=NULL;
 assert(!esp32_mquickjs_wifi_nan_pairing_receive(d,&p)&&p->status.identity==identity);
 esp32_mquickjs_wifi_nan_pairing_unreserve(p);esp32_mquickjs_wifi_nan_pairing_release(p);
 finish();assert(!pair_starts&&!bootstrap_sends);
}
