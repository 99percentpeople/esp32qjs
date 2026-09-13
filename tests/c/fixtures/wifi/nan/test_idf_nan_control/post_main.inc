
static void receive_notice(uint32_t identity,const esp32_mquickjs_wifi_nan_sdk_notice_t *notice,void *p){
    assert(identity&&p==&notified&&!posted&&!observer_depth);
    assert(notice->kind==ESP32_MQUICKJS_NAN_SDK_EVENT&&notice->event_id==7&&notice->size==4);
    assert(*(const uint32_t *)notice->data==42);notified++;
}
int main(void){uint32_t identity=0,data=42;
    assert(esp32_mquickjs_wifi_nan_sdk_observe(receive_notice,&notified,&identity)==ESP_OK);
    nan_app_post_event(7,&data,sizeof(data));assert(notified==1&&posted==1&&!wait_ticks);
    esp32_mquickjs_wifi_nan_sdk_observer_status_t status;
    esp32_mquickjs_wifi_nan_sdk_observer_status(&status);assert(status.dropped_events==1);
    assert(esp32_mquickjs_wifi_nan_sdk_unobserve(&identity)==ESP_OK);
}
