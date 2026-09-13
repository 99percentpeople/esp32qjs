
int main(void){
 setup();uint8_t mac[6]={2,3,4,5,6,7};esp32_mquickjs_wifi_nan_pairing_t*p=NULL;
 assert(!esp32_mquickjs_wifi_nan_pairing_create(d,9,mac,true,30000,&p));
 assert(!esp32_mquickjs_wifi_nan_pairing_select(p,77));assert(!esp32_mquickjs_wifi_nan_pairing_activate(p));tick();
 assert(!pair_starts&&p->status.verification);
 assert(esp32_mquickjs_wifi_nan_pairing_confirm(p,true,123456)==ESP_ERR_INVALID_STATE);
 assert(!esp32_mquickjs_wifi_nan_pairing_confirm(p,true,0));tick();assert(pair_starts==1&&!seen_pin&&seen_credential==77);
 pair_native.paired=true;pair_native.completed_us=clock_us;pair_native.traffic_pending=true;tick();assert(!pair_commits);
 pair_native.traffic_pending=false;tick();assert(p->status.ready&&p->status.credential_id==301&&pair_commits==1);tick();assert(pair_commits==1);
 esp32_mquickjs_wifi_nan_pairing_close(p,false);tick();esp32_mquickjs_wifi_nan_pairing_release(p);finish();
}
