
int main(void){
 setup();esp32_mquickjs_wifi_nan_pairing_t*p=prepare(1000);
 assert(!esp32_mquickjs_wifi_nan_pairing_confirm(p,true,123456));tick();
 pair_native.authenticated=pair_native.paired=true;pair_native.completed_us=p->deadline_us-1;
 clock_us=p->deadline_us+100000;tick();assert(p->status.ready&&!p->status.timed_out);
 esp32_mquickjs_wifi_nan_pairing_close(p,false);tick();esp32_mquickjs_wifi_nan_pairing_release(p);finish();
}
