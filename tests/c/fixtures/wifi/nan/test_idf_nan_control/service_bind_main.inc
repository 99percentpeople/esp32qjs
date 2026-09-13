
static void bound_notice(uint32_t identity,const esp32_mquickjs_wifi_nan_sdk_notice_t*n,void*opaque){
 assert(identity&&opaque==&notifications&&!depth&&!observer_depth&&n->kind==ESP32_MQUICKJS_NAN_SDK_SERVICE_BOUND&&n->service_id==7);
 if(missing_host)assert(n->status==ESP_ERR_INVALID_RESPONSE);
 else assert(!n->status&&own.svc_id==7&&n->context==own.type&&esp32qjs_nan_is_discovery_only(7));
 ++notifications;
}
int main(void){
 uint32_t observer=0;assert(!esp32_mquickjs_wifi_nan_sdk_observe(bound_notice,&notifications,&observer));
 wifi_nan_publish_cfg_t config={.service_name="test"};uint8_t id=0;
 own=(struct own_svc_info){255,2};assert(!__wrap_nan_start_publish_service(&config,&id)&&id==7&&notifications==1);
 own=(struct own_svc_info){255,1};id=0;assert(!__wrap_nan_start_subscribe_service(&config,&id)&&id==7&&notifications==2);
 own.svc_id=255;id=0;create_result=-71;assert(__wrap_nan_start_subscribe_service(&config,&id)==-71&&!id&&notifications==2);
 create_result=0;missing_host=true;assert(!__wrap_nan_start_subscribe_service(&config,&id)&&id==7&&notifications==3);
 assert(!esp32_mquickjs_wifi_nan_sdk_unobserve(&observer));
}
