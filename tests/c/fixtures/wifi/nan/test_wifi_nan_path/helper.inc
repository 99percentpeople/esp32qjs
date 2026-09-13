
static esp32_mquickjs_wifi_nan_session_t*s;
static esp32_mquickjs_wifi_nan_discovery_t*d;
static struct esp32_mquickjs_event_queue q={.references=1};
static void tick(void){clock_us+=100000;assert(esp32_mquickjs_wifi_nan_service());run_queued(NULL);}
static void setup(bool publish){
 assert(!esp32_mquickjs_wifi_nan_open_runtime());wifi_nan_sync_config_t config={.op_channel=6};
 assert(!esp32_mquickjs_wifi_nan_session_create(&config,1000,&s));
 assert(!esp32_mquickjs_wifi_nan_session_activate(s));tick();
 esp32_mquickjs_wifi_nan_service_config_t cfg={.publish={.service_name="ndp",.datapath_reqd=true}};
 assert(!esp32_mquickjs_wifi_nan_discovery_create(s,publish,&cfg,1000,&d));
 assert(!esp32_mquickjs_wifi_nan_discovery_activate(d,&q));tick();assert(d->status.ready);
}
void incoming(void){
 assert(!path_native[0].identity);
 path_native[0]=(esp32_mquickjs_wifi_nan_ndp_status_t){.identity=path_next_native++,.service_id=7,
  .publisher_id=7,.ndp_id=9,.peer={2,3,4,5,6,7},.native_bound=true,.host_bound=true,.started_us=clock_us};
}
static void finish(void){
 if(!s->status.retired){esp32_mquickjs_wifi_nan_session_close(s,false);tick();}
 esp32_mquickjs_wifi_nan_discovery_queue_closed(d);esp32_mquickjs_wifi_nan_discovery_release(d);
 esp32_mquickjs_wifi_nan_session_release(s);assert(!allocations&&!s_nan_path_handles);
}
