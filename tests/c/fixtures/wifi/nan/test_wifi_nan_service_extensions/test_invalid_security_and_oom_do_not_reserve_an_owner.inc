
int main(void){
 start(false);wifi_nan_discovery_security_params_t sec={.num_credentials=1,.creds={{.csid=1}}};
 strcpy(sec.creds[0].passphrase,"short");
 esp32_mquickjs_wifi_nan_service_config_t cfg={.publish={.service_name="secure",.security_reqd=true,.security_cfg=&sec}};
 esp32_mquickjs_wifi_nan_discovery_t*d=NULL;unsigned refs=s->references,alloc=allocation_calls;
 assert(esp32_mquickjs_wifi_nan_discovery_create(s,true,&cfg,1000,&d)==ESP_ERR_INVALID_ARG);
 assert(!d&&!s_nan_discovery_handles&&s->references==refs&&allocation_calls==alloc);
 strcpy(sec.creds[0].passphrase,"test-only-password");sec.creds[0].csid=2;
 assert(esp32_mquickjs_wifi_nan_discovery_create(s,true,&cfg,1000,&d)==ESP_ERR_NOT_SUPPORTED);
 sec.creds[0].csid=1;fail_allocation=allocation_calls+1;
 assert(esp32_mquickjs_wifi_nan_discovery_create(s,true,&cfg,1000,&d)==ESP_ERR_NO_MEM);
 assert(!d&&!s_nan_discovery_handles&&s->references==refs);
 esp32_mquickjs_wifi_nan_session_close(s,false);tick();esp32_mquickjs_wifi_nan_session_release(s);assert(!allocations);
}
