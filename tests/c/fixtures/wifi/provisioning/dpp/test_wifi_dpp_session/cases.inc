
static void run_worker(void){assert(scheduled);void(*fn)(void*)=scheduled;scheduled=NULL;fn(argument);argument=NULL;}
static void tick(void){now+=100001;esp32_mquickjs_wifi_dpp_service();if(scheduled)run_worker();}
static void reset(void){assert(!s_dpp_active&&!s_dpp_handles&&!s_dpp_workers&&!scheduled&&!helper_identity&&!radio_active&&allocations==frees);
 fail_alloc=fail_queue=hold_native=hold_helper=terminal=uri_generated=helper_ready=false;begin_error=row_error=0;copies=commits=starts=0;
 during_copy=NULL;authentication_error=0;hold_connection=false;selections=connections=connection_ends=connection_terminal=0;
 restore_error=recover_error=0;recoveries=0;
 assert(!native_connection);assert(!esp32_mquickjs_wifi_dpp_open_runtime());}
static esp32_mquickjs_wifi_dpp_session_t*create(void){
 esp32_mquickjs_wifi_dpp_worker_options_t o={.channels="6"};esp32_mquickjs_wifi_dpp_session_t*s=NULL;
 assert(!esp32_mquickjs_wifi_dpp_session_create(&o,false,1000,&s));assert(!esp32_mquickjs_wifi_dpp_session_activate(s));return s;}
static void close_all(esp32_mquickjs_wifi_dpp_session_t*s){esp32_mquickjs_wifi_dpp_session_close(s,false);
 for(unsigned i=0;i<8&&!s->status.retired;i++)tick();assert(s->status.retired&&!helper_identity&&!radio_active);
 esp32_mquickjs_wifi_dpp_session_release(s);assert(allocations==frees);}
static void close_in_copy(void){esp32_mquickjs_wifi_dpp_session_close(s_dpp_active,false);}
static void finish_capture(void){uri_generated=true;radio.worker.native.uri_available=true;tick();terminal=true;tick();tick();}
int main(void){
 reset();esp32_mquickjs_wifi_dpp_session_t*s=create();tick();assert(!s->status.listening);
 uri_generated=true;radio.worker.native.uri_available=true;tick();assert(s->status.listening&&s->status.uri_ready);
 char uri[32];assert(esp32_mquickjs_wifi_dpp_session_uri(s,uri,6,false)==ESP_ERR_INVALID_SIZE);
 assert(!esp32_mquickjs_wifi_dpp_session_uri(s,uri,sizeof(uri),false)&&!strcmp(uri,"DPP:X;"));
 assert(!esp32_mquickjs_wifi_dpp_session_uri(s,NULL,0,true));
 terminal=true;hold_native=true;tick();assert(!s->status.capture_finished&&!copies);
 hold_native=false;tick();hold_helper=true;tick();assert(!s->status.configs_ready&&!copies);
 hold_helper=false;tick();assert(s->status.configs_ready&&s->status.config_count==2&&copies==2&&commits==1);
 esp_dpp_config_data_t row;assert(!esp32_mquickjs_wifi_dpp_session_config(s,1,&row)&&row.ssid[0]=='B'&&row.key[0]==2);
 esp32_mquickjs_wifi_dpp_session_status_t observed;assert(esp32_mquickjs_wifi_dpp_session_wait_begin(s,false));
 assert(!esp32_mquickjs_wifi_dpp_session_observation(s,&observed));assert(!esp32_mquickjs_wifi_dpp_session_configs_commit(s));
 esp32_mquickjs_wifi_dpp_session_wait_end(s,false);assert(esp32_mquickjs_wifi_dpp_session_observation(s,&observed));
 assert(!esp32_mquickjs_wifi_dpp_session_config(s,0,&row)&&row.key[0]==1);close_all(s);reset();
 /* Selection uses the real scheduler and cannot complete on IP before the
  * native authentication check. Close must drain Station before native DPP. */
 s=create();finish_capture();assert(!esp32_mquickjs_wifi_dpp_session_configs_commit(s));
 assert(esp32_mquickjs_wifi_dpp_session_connect(s,2,0,1000,false)==ESP_ERR_INVALID_ARG&&!selections);
 assert(!esp32_mquickjs_wifi_dpp_session_connect(s,0,0,1000,false));tick();tick();
 assert(selections==1&&connections==1&&!s->status.connected);
 authentication_error=ESP_ERR_NOT_FINISHED;connection_terminal=ESP32_MQUICKJS_WIFI_CONNECT_EVENT_KIND_SUCCESS;tick();
 assert(s->status.connection_established&&!s->status.connection_verified&&!s->status.connected);
 authentication_error=0;tick();tick();assert(s->status.connected&&s->status.connection_verified);
 esp32_mquickjs_wifi_link_snapshot_t link;double elapsed;assert(!esp32_mquickjs_wifi_dpp_session_connection_result(s,&link,&elapsed)&&link.bssid[0]==2&&elapsed>0);
 assert(!esp32_mquickjs_wifi_dpp_session_connect(s,0,0,1000,false)&&selections==1&&connections==1);
 assert(esp32_mquickjs_wifi_dpp_session_connect(s,1,0,1000,false)==ESP_ERR_INVALID_STATE);
 hold_connection=true;esp32_mquickjs_wifi_dpp_session_close(s,false);tick();assert(native_connection&&!radio.worker.retired);
 hold_connection=false;close_all(s);assert(connection_ends==1);reset();
 s=create();finish_capture();assert(!esp32_mquickjs_wifi_dpp_session_configs_commit(s));
 assert(!esp32_mquickjs_wifi_dpp_session_connect(s,0,0,1000,false));tick();tick();
 connection_terminal=ESP32_MQUICKJS_WIFI_CONNECT_EVENT_KIND_SUCCESS;authentication_error=ESP_ERR_INVALID_RESPONSE;tick();
 assert(s->status.closing&&!s->status.connected&&native_connection);close_all(s);reset();
 s=create();finish_capture();assert(!esp32_mquickjs_wifi_dpp_session_configs_commit(s));
 assert(!esp32_mquickjs_wifi_dpp_session_connect(s,0,0,1000,false));tick();tick();s->deadline_us=now;
 esp32_mquickjs_wifi_dpp_service();if(scheduled)run_worker();assert(s->status.timed_out);close_all(s);reset();
 /* Known START failure parks the real service until explicit recovery.
  * Two requests before/during dispatch authorize only one native retry. */
 s=create();finish_capture();assert(esp32_mquickjs_wifi_dpp_session_recover(s)==ESP_ERR_INVALID_STATE);
 restore_error=88;esp32_mquickjs_wifi_dpp_session_close(s,false);tick();
 assert(s->status.native.restore_start_failed&&!s->status.retired&&!recoveries);
 tick();assert(!scheduled&&!recoveries&&s->status.cleanup_error==88);
 /* Queued admission can still be rejected after another source changes.
  * A drained capture helper alone must not bypass native-close restoration. */
 recover_error=77;assert(!esp32_mquickjs_wifi_dpp_session_recover(s));tick();
 assert(recoveries==1&&s->status.cleanup_error==77&&!s->status.native_closed&&radio_active&&helper_identity);
 assert(!s->status.recovery_pending);recover_error=0;recoveries=0;
 assert(!esp32_mquickjs_wifi_dpp_session_recover(s)&&s->status.recovery_pending&&!recoveries);
 assert(!esp32_mquickjs_wifi_dpp_session_recover(s));hold_native=true;tick();
 assert(recoveries==1&&s->status.recovery_pending&&!s->status.retired);
 assert(!esp32_mquickjs_wifi_dpp_session_recover(s));tick();assert(recoveries==1);
 assert(!esp32_mquickjs_wifi_dpp_prepare_runtime_destroy());
 assert(esp32_mquickjs_wifi_dpp_session_recover(s)==ESP_ERR_INVALID_STATE);
 if(scheduled)run_worker();hold_native=false;close_all(s);reset();
 /* Cancel before dispatch never admits a native protocol. */
 s=create();assert(esp32_mquickjs_wifi_dpp_service()&&scheduled);esp32_mquickjs_wifi_dpp_session_close(s,false);
 run_worker();close_all(s);assert(!starts);reset();
 s=create();fail_queue=true;assert(!esp32_mquickjs_wifi_dpp_service()&&!scheduled&&helper_identity);
 now+=2000000;esp32_mquickjs_wifi_dpp_service();assert(s->status.timed_out&&s->status.retired&&!starts);
 esp32_mquickjs_wifi_dpp_session_release(s);reset();
 /* Unknown/pending native close prevents runtime/helper retirement. */
 s=create();tick();hold_native=true;assert(!esp32_mquickjs_wifi_dpp_prepare_runtime_destroy());if(scheduled)run_worker();
 assert(helper_identity&&radio_active&&!s->status.retired);hold_native=false;tick();hold_helper=true;tick();
 assert(radio_active);hold_helper=false;close_all(s);reset();
 /* Partial row failure or close during copy never publishes partial secrets. */
 s=create();tick();row_error=77;finish_capture();assert(s->status.closing&&s->status.error==77&&!s->status.configs_ready);
 close_all(s);reset();s=create();tick();during_copy=close_in_copy;finish_capture();
 assert(s->status.closing&&!s->status.configs_ready);for(size_t i=0;i<sizeof(s->configs);i++)assert(!((uint8_t*)s->configs)[i]);
 close_all(s);reset();
 /* GC drops JS ownership while an independent worker still uses the object. */
 s=create();assert(esp32_mquickjs_wifi_dpp_service()&&scheduled);esp32_mquickjs_wifi_dpp_session_release(s);run_worker();
 for(unsigned i=0;i<8&&s_dpp_active;i++)tick();reset();return 0;
}
