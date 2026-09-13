
int main(void){
 start(true);
 wifi_nan_discovery_security_params_t sec={.num_credentials=1,.creds={{.csid=1,.use_pmk=true}}};
 memset(sec.creds[0].pmk,0x67,32);
 uint8_t body[255];memset(body,0x34,sizeof(body));
 nan_vendor_ie_t vendor={.vendor_oui={0x50,0x6f,0x9a},.body_len=255,.body=body};
 esp32_mquickjs_wifi_nan_service_config_t cfg={.subscribe={.service_name="secure",.datapath_reqd=true,
  .security_reqd=true,.security_cfg=&sec,.vendor_ie=&vendor}};
 esp32_mquickjs_wifi_nan_discovery_t*d=NULL;
 assert(!esp32_mquickjs_wifi_nan_discovery_create(s,false,&cfg,1000,&d));
 wifi_nan_discovery_security_params_t*captured=d->config.subscribe.security_cfg;
 assert(captured!=&sec&&captured->creds[0].pmk[0]==0x67);
 assert(captured->group_data_prot&&captured->group_mgmt_prot&&d->status.security_required);
 memset(&sec,0,sizeof(sec));memset(body,0,sizeof(body));
 assert(captured->creds[0].pmk[31]==0x67&&d->vendor.body!=body&&d->vendor.body[254]==0x34);
 esp32_mquickjs_event_queue_t q={.references=1};
 assert(!esp32_mquickjs_wifi_nan_discovery_activate(d,&q));tick();assert(d->status.ready);
 wifi_nan_datapath_req_t req={.pub_id=3,.peer_mac={2,3,4,5,6,7},.confirm_required=false};
 esp32_mquickjs_wifi_nan_path_t*p=NULL;
 assert(!esp32_mquickjs_wifi_nan_path_create(d,&req,1000,&p));
 assert(esp32_mquickjs_wifi_nan_path_activate(p)==ESP_ERR_INVALID_STATE&&!path_requests);
 esp32_mquickjs_wifi_nan_path_close(p,false);esp32_mquickjs_wifi_nan_path_release(p);
 esp32_mquickjs_wifi_nan_session_close(s,false);tick();assert(d->status.retired);
 for(size_t i=0;i<sizeof(*captured);i++)assert(!((uint8_t*)captured)[i]);
 assert(d->status.credential_count==1&&d->status.vendor_bytes==255);
 esp32_mquickjs_wifi_nan_discovery_queue_closed(d);esp32_mquickjs_wifi_nan_discovery_release(d);
 esp32_mquickjs_wifi_nan_session_release(s);assert(!allocations);
}
