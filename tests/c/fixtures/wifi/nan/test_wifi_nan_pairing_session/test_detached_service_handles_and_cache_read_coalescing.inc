
int main(void){
 setup();uint32_t rev=99,other=99;bool ready=false;esp32_mquickjs_wifi_nan_credentials_t list;
 cached_credentials.count=1;cached_credentials.entries[0].identity=301;
 assert(!esp32_mquickjs_wifi_nan_discovery_credentials_request(d,&rev));
 assert(!esp32_mquickjs_wifi_nan_discovery_credentials_request(d,&other)&&rev==other);
 assert(!esp32_mquickjs_wifi_nan_discovery_credentials_read(d,rev,&ready,&list)&&!ready);tick();
 assert(credential_reads==1&&!esp32_mquickjs_wifi_nan_discovery_credentials_read(d,rev,&ready,&list)&&ready&&list.count==1);
 esp32_mquickjs_wifi_nan_pairing_t*p=prepare(1000);esp32_mquickjs_wifi_nan_session_close(s,false);tick();
 assert(p->status.retired&&!d->parent);esp32_mquickjs_wifi_nan_pairing_status_t status;
 esp32_mquickjs_wifi_nan_pairing_status(p,&status);assert(status.retired);
 esp32_mquickjs_wifi_nan_pairing_close(p,false);
 assert(esp32_mquickjs_wifi_nan_pairing_confirm(p,true,0)==ESP_ERR_INVALID_STATE);
 esp32_mquickjs_wifi_nan_pairing_t*next=NULL;uint8_t mac[6]={2,3,4,5,6,7};
 assert(esp32_mquickjs_wifi_nan_pairing_create(d,9,mac,true,1000,&next)==ESP_ERR_INVALID_STATE);
 assert(esp32_mquickjs_wifi_nan_discovery_credentials_request(d,&rev)==ESP_ERR_INVALID_STATE);
 esp32_mquickjs_wifi_nan_pairing_release(p);finish();
}
