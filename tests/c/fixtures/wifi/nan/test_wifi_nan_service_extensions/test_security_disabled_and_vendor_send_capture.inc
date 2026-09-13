
int main(void){
 start(false);wifi_nan_discovery_security_params_t sec={.num_credentials=1,.creds={{.csid=1,.use_pmk=true}}};
 esp32_mquickjs_wifi_nan_service_config_t cfg={.publish={.service_name="vendor",.security_reqd=true,.security_cfg=&sec}};
 esp32_mquickjs_wifi_nan_discovery_t*d=NULL;
 assert(esp32_mquickjs_wifi_nan_discovery_create(s,true,&cfg,1000,&d)==ESP_ERR_NOT_SUPPORTED&&!d);
 cfg.publish.security_reqd=false;cfg.publish.security_cfg=NULL;
 assert(!esp32_mquickjs_wifi_nan_discovery_create(s,true,&cfg,1000,&d));
 esp32_mquickjs_event_queue_t q={.references=1};assert(!esp32_mquickjs_wifi_nan_discovery_activate(d,&q));tick();
 uint8_t body[2]={1,2};nan_vendor_ie_t vendor={.vendor_oui={1,2,3},.body_len=2,.body=body};
 wifi_nan_followup_params_t params={.peer_inst_id=3,.peer_mac={2,3,4,5,6,7},.vendor_ie=&vendor};
 esp32_mquickjs_wifi_nan_message_t*m=NULL;
 assert(!esp32_mquickjs_wifi_nan_message_create(d,&params,1000,&m));
 body[0]=9;assert(m->params.vendor_ie!=&vendor&&m->params.vendor_ie->body[0]==1);
 esp32_mquickjs_wifi_nan_message_cancel(m,false);esp32_mquickjs_wifi_nan_message_release(m);
 vendor.body_len=256;m=NULL;
 assert(esp32_mquickjs_wifi_nan_message_create(d,&params,1000,&m)==ESP_ERR_INVALID_ARG);
 esp32_mquickjs_wifi_nan_session_close(s,false);tick();
 esp32_mquickjs_wifi_nan_discovery_queue_closed(d);esp32_mquickjs_wifi_nan_discovery_release(d);
 esp32_mquickjs_wifi_nan_session_release(s);assert(!allocations);
}
