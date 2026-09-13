
int main(void){
 setup();uint8_t mac[6]={2,3,4,5,6,7};esp32_mquickjs_wifi_nan_pairing_t*p=NULL;
 unsigned refs=d->references;fail_allocation=allocation_calls+1;
 assert(esp32_mquickjs_wifi_nan_pairing_create(d,9,mac,true,1000,&p)==ESP_ERR_NO_MEM);
 assert(!p&&!s_nan_pairing_handles&&d->references==refs);
 fail_allocation=0;s_nan_pairing_next_identity=UINT32_MAX;p=prepare(1000);
 assert(p->status.identity==UINT32_MAX&&!s_nan_pairing_next_identity);
 esp32_mquickjs_wifi_nan_pairing_close(p,false);tick();esp32_mquickjs_wifi_nan_pairing_release(p);p=NULL;
 assert(esp32_mquickjs_wifi_nan_pairing_create(d,9,mac,true,1000,&p)==ESP_ERR_NO_MEM&&!p);
 assert(!s_nan_pairing_handles&&d->references==refs);finish();
}
