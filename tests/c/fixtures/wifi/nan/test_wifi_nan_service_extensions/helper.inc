
static esp32_mquickjs_wifi_nan_session_t*s;
static void tick(void){clock_us+=100000;if(esp32_mquickjs_wifi_nan_service())run_queued(NULL);}
static void start(bool group){
 assert(!esp32_mquickjs_wifi_nan_open_runtime());
 wifi_nan_sync_config_t config={.op_channel=6,.group_mgmt_prot=group};
 assert(!esp32_mquickjs_wifi_nan_session_create(&config,1000,&s));
 assert(!esp32_mquickjs_wifi_nan_session_activate(s));tick();assert(s->status.ready);
 for(size_t i=0;i<sizeof(s->config);i++)assert(!((uint8_t*)&s->config)[i]);
 assert(s->status.group_management_protection==group);
}
