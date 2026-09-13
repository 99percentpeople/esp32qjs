
static void receive_notice(uint32_t identity,const esp32_mquickjs_wifi_nan_sdk_notice_t *n,void *p){
    assert(identity&&p==&notifications&&!observer_depth&&resets==0);
    assert(n->kind==ESP32_MQUICKJS_NAN_SDK_NDP_TERMINATED&&n->ndp_id==9&&n->status==7);
    assert(n->peer[0]==2&&(bits&NDP_TERMINATED));++notifications;
}
int main(void){uint32_t identity=0;uint8_t mac[6]={2,3,4,5,6,7};
    assert(esp32_mquickjs_wifi_nan_sdk_observe(receive_notice,&notifications,&identity)==ESP_OK);
    nan_app_ndp_terminated_cb(7,9,mac);assert((bits&NDP_TERMINATED)&&notifications==1);
    assert(esp32_mquickjs_wifi_nan_sdk_unobserve(&identity)==ESP_OK);}
