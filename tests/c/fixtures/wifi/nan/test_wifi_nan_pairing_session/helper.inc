
static esp32_mquickjs_wifi_nan_session_t*s;
static esp32_mquickjs_wifi_nan_discovery_t*d;
static struct esp32_mquickjs_event_queue q={.references=1};
static void tick(void){clock_us+=100000;assert(esp32_mquickjs_wifi_nan_service());run_queued(NULL);}
static void setup(void){
 assert(!esp32_mquickjs_wifi_nan_open_runtime());wifi_nan_sync_config_t config={.op_channel=6};
 assert(!esp32_mquickjs_wifi_nan_session_create(&config,1000,&s));
 assert(!esp32_mquickjs_wifi_nan_session_activate(s));tick();
 wifi_nan_pairing_cfg_t pairing={.pairing_setup=true,.npk_nik_caching=true,.pairing_verification=true,.bootstrapping_methods=WIFI_NAN_BOOTSTRAP_PIN_CODE_KEYPAD};
 esp32_mquickjs_wifi_nan_service_config_t cfg={.subscribe={.service_name="pin-pairing",.pairing=&pairing}};
 assert(!esp32_mquickjs_wifi_nan_discovery_create(s,false,&cfg,1000,&d));
 pairing.pairing_setup=false;assert(d->config.subscribe.pairing->pairing_setup);
 assert(d->config.subscribe.security_reqd&&d->config.subscribe.security_cfg->num_credentials==1);
 assert(d->config.subscribe.security_cfg->creds[0].csid==WIFI_NAN_CSID_NCS_PK_PASN_128);
 assert(d->status.security_required&&!d->status.credential_count);
 assert(!esp32_mquickjs_wifi_nan_discovery_activate(d,&q));tick();assert(d->status.ready);
}
static __attribute__((unused)) esp32_mquickjs_wifi_nan_pairing_t*prepare(uint32_t timeout){
 uint8_t mac[6]={2,3,4,5,6,7};esp32_mquickjs_wifi_nan_pairing_t*p=NULL;
 assert(!esp32_mquickjs_wifi_nan_pairing_create(d,9,mac,true,timeout,&p));
 assert(!esp32_mquickjs_wifi_nan_pairing_activate(p));return p;
}
static void finish(void){
 if(!s->status.retired){esp32_mquickjs_wifi_nan_session_close(s,false);tick();}
 assert(q.closed);esp32_mquickjs_wifi_nan_discovery_queue_closed(d);
 esp32_mquickjs_wifi_nan_discovery_release(d);esp32_mquickjs_wifi_nan_session_release(s);
 assert(!s_nan_pairing_handles&&!s_nan_discovery_handles&&!s_nan_handles&&!allocations&&q.references==1);
}
