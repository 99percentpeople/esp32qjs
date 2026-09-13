
int main(void){
 setup();esp32_mquickjs_wifi_nan_pairing_t*p=request(30000);
 accepted(9);assert(!p->status.peer_responded);tick();tick();
 assert(bootstrap_sends==1&&!bootstrap_response&&!pair_starts&&p->status.bootstrap_attempted);
 accepted(8);assert(!p->status.peer_responded);
 accepted(9);assert(p->status.peer_accepted&&!p->status.confirmed);tick();assert(!pair_starts);
 assert(!esp32_mquickjs_wifi_nan_pairing_confirm(p,true,123456));tick();assert(!pair_starts);
 recycle(true);assert(pair_starts==1&&p->status.bootstrap_sent&&!p->status.bootstrap_tx_pending);
 pair_native.authenticated=pair_native.paired=true;pair_native.completed_us=clock_us;tick();
 assert(p->status.ready&&pair_commits==1);
 esp32_mquickjs_wifi_nan_pairing_close(p,false);tick();assert(p->status.retired);
 esp32_mquickjs_wifi_nan_pairing_release(p);
 uint8_t mac[6]={2,3,4,5,6,7};p=NULL;
 assert(!esp32_mquickjs_wifi_nan_pairing_create(d,9,mac,true,1000,&p));
 assert(!esp32_mquickjs_wifi_nan_pairing_request(p));
 assert(esp32_mquickjs_wifi_nan_pairing_activate(p)==ESP_ERR_INVALID_STATE);
 accepted(9);assert(!p->status.peer_responded&&pair_starts==1);
 esp32_mquickjs_wifi_nan_pairing_release(p);finish();
}
