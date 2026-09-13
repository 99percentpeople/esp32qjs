
static void run_worker(void){
 assert(scheduled);void (*fn)(void*)=scheduled;void *arg=scheduled_arg;
 scheduled=NULL;scheduled_arg=NULL;fn(arg);
}
static void tick(void){now+=100000;(void)esp32_mquickjs_wifi_wps_ap_service();if(scheduled)run_worker();}
static esp32_mquickjs_wifi_wps_ap_session_t *create(void){
 esp_wps_config_t config={.wps_type=WPS_TYPE_PIN};esp32_mquickjs_wifi_wps_ap_session_t *s=NULL;
 assert(!esp32_mquickjs_wifi_wps_ap_session_create(&config,5000,&s));return s;
}
static void finish(esp32_mquickjs_wifi_wps_ap_session_t *s){
 esp32_mquickjs_wifi_wps_ap_session_close(s,false);
 for(int i=0;i<8 && !s->status.retired;i++)tick();
 assert(s->status.retired && !helper_identity && !radio_active && !s_wps_active);
 esp32_mquickjs_wifi_wps_ap_session_release(s);assert(allocs==frees);
}
int main(void){
 assert(!esp32_mquickjs_wifi_wps_ap_open_runtime());
 esp32_mquickjs_wifi_wps_ap_session_t *s=create();reserve_error=88;
 assert(esp32_mquickjs_wifi_wps_ap_session_activate(s)==88 && s->status.retired && !helper_identity);
 esp32_mquickjs_wifi_wps_ap_session_release(s);reserve_error=0;
 s=create();assert(!esp32_mquickjs_wifi_wps_ap_session_activate(s) && helper_identity);
 tick();uint8_t pin[8],peer[6];
 assert(!esp32_mquickjs_wifi_wps_ap_session_pin(s,pin,false) && !memcmp(pin,"12345670",8));
 assert(!esp32_mquickjs_wifi_wps_ap_session_pin(s,NULL,true));
 capture_ready=true;finish_error=ESP_ERR_NOT_FINISHED;tick();
 assert(!s->status.result_ready && helper_identity && radio_active);
 finish_error=0;tick();assert(s->status.capture_finished && s->status.result_ready && !helper_drains);
 assert(!esp32_mquickjs_wifi_wps_ap_session_registered(s,peer,false) && !memcmp(peer,"ABCDEF",6));
 assert(!esp32_mquickjs_wifi_wps_ap_session_registered(s,peer,false));
 esp32_mquickjs_wifi_wps_ap_session_status_t observed;
 assert(esp32_mquickjs_wifi_wps_ap_session_wait_begin(s,false));
 assert(!esp32_mquickjs_wifi_wps_ap_session_observation(s,&observed));
 assert(!esp32_mquickjs_wifi_wps_ap_session_registered(s,NULL,true));
 esp32_mquickjs_wifi_wps_ap_session_wait_end(s,false);
 assert(s->status.result_consumed && !s->status.result_ready);
 hold_close=true;esp32_mquickjs_wifi_wps_ap_session_close(s,false);tick();
 assert(helper_identity && radio_active && !helper_releases);
 hold_close=false;tick();hold_helper=true;tick();
 assert(!radio_active && helper_identity && !s->status.retired);
 hold_helper=false;finish(s);capture_ready=false;

 /* Cancel before dispatch never enables SDK; queue pressure does not lose
  * the registry's native cleanup ownership or prevent a Session deadline. */
 unsigned old_starts=starts;s=create();assert(!esp32_mquickjs_wifi_wps_ap_session_activate(s));
 assert(esp32_mquickjs_wifi_wps_ap_service() && scheduled);
 esp32_mquickjs_wifi_wps_ap_session_close(s,false);run_worker();finish(s);assert(starts==old_starts);
 s=create();assert(!esp32_mquickjs_wifi_wps_ap_session_activate(s));queue_fail=true;
 assert(!esp32_mquickjs_wifi_wps_ap_service() && s->status.cleanup_error==ESP_ERR_NO_MEM);
 now+=6000000;(void)esp32_mquickjs_wifi_wps_ap_service();
 assert(s->status.retired && s->status.timed_out && !helper_identity && starts==old_starts);
 esp32_mquickjs_wifi_wps_ap_session_release(s);queue_fail=false;

 /* A rejected native admission with a token still owes Radio cleanup. */
 s=create();assert(!esp32_mquickjs_wifi_wps_ap_session_activate(s));start_error=71;partial_start=true;hold_close=true;
 tick();assert(s->status.error==71 && s->status.closing && radio_active && helper_identity);
 start_error=0;partial_start=false;hold_close=false;finish(s);
 s=create();assert(!esp32_mquickjs_wifi_wps_ap_session_activate(s));tick();hold_close=true;
 assert(!esp32_mquickjs_wifi_wps_ap_prepare_runtime_destroy());if(scheduled)run_worker();
 assert(radio_active && helper_identity);hold_close=false;finish(s);
 assert(esp32_mquickjs_wifi_wps_ap_prepare_runtime_destroy());
 assert(allocs==frees && !helper_drains);return 0;
}
