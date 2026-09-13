
int main(void){
 setup();q.full=true;incoming();assert(d->status.pairing_request_pending);
 incoming();assert(d->status.pairing_requests_dropped==1);tick();
 assert(s->pairing&&!pair_starts&&!bootstrap_sends);
 esp32_mquickjs_wifi_nan_pairing_t*p=NULL;
 assert(!esp32_mquickjs_wifi_nan_pairing_receive(d,&p)&&p);
 assert(esp32_mquickjs_wifi_nan_pairing_confirm(p,true,123456)==ESP_ERR_INVALID_STATE);
 uint32_t identity=p->status.identity;
 esp32_mquickjs_wifi_nan_pairing_unreserve(p);esp32_mquickjs_wifi_nan_pairing_release(p);p=NULL;
 tick();assert(s->pairing&&!s->pairing->status.closing&&d->status.pairing_request_pending);
 assert(!esp32_mquickjs_wifi_nan_pairing_receive(d,&p)&&p->status.identity==identity);
 assert(esp32_mquickjs_wifi_nan_pairing_deliver(p)&&!d->status.pairing_request_pending);
 assert(!esp32_mquickjs_wifi_nan_pairing_confirm(p,true,123456));tick();
 assert(pair_starts==1&&!bootstrap_sends);tick();tick();
 assert(bootstrap_sends==1&&bootstrap_response&&bootstrap_accept);
 pair_native.authenticated=pair_native.paired=true;pair_native.completed_us=clock_us;tick();
 assert(!p->status.ready&&!pair_commits);recycle(true);assert(p->status.ready&&pair_commits==1);
 esp32_mquickjs_wifi_nan_pairing_close(p,false);tick();esp32_mquickjs_wifi_nan_pairing_release(p);
 incoming();assert(!s->pairing_request.service&&d->status.pairing_requests_dropped==2);finish();
}
