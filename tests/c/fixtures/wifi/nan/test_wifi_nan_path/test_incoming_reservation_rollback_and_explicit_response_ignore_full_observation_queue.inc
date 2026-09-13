
int main(void){
 setup(true);q.full=true;incoming();tick();
 esp32_mquickjs_wifi_nan_path_t*p=NULL,*other=NULL;
 assert(!esp32_mquickjs_wifi_nan_path_receive(d,&p)&&p&&!p->status.delivered);
 assert(!esp32_mquickjs_wifi_nan_path_receive(d,&other)&&!other);
 esp32_mquickjs_wifi_nan_path_unreserve(p);esp32_mquickjs_wifi_nan_path_release(p);p=NULL;
 assert(!esp32_mquickjs_wifi_nan_path_receive(d,&p)&&p);assert(esp32_mquickjs_wifi_nan_path_deliver(p));
 assert(!esp32_mquickjs_wifi_nan_path_receive(d,&other)&&!other);
 uint8_t ssi[2]={4,5};assert(!esp32_mquickjs_wifi_nan_path_respond(p,true,ssi,2));
 assert(esp32_mquickjs_wifi_nan_path_respond(p,true,ssi,2)==ESP_ERR_INVALID_STATE);
 tick();assert(path_responses==1&&path_response_accept&&!p->status.ready);
 path_native[0].accepted=true;path_native[0].completed_us=clock_us;
 esp32_mquickjs_wifi_nan_path_status_t state;esp32_mquickjs_wifi_nan_path_status(p,&state);assert(state.ready);
 esp32_mquickjs_wifi_nan_path_close(p,false);tick();
 path_native[0].native_deleted=path_native[0].frames_retired=true;tick();
 assert(p->status.retired&&!s->status.closing);esp32_mquickjs_wifi_nan_path_release(p);finish();
}
