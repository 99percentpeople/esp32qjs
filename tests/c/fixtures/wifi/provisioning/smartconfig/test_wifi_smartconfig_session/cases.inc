
static esp32_mquickjs_wifi_smartconfig_session_t *current;
static esp32_mquickjs_wifi_smartconfig_decoder_options_t options={.type=SC_TYPE_ESPTOUCH,.channel_timeout_s=60};
static void run_worker(void){assert(scheduled);void (*fn)(void *)=scheduled;void *arg=scheduled_arg;scheduled=NULL;scheduled_arg=NULL;fn(arg);assert(!s_sc_workers);}
static void tick(void){now+=200000;assert(esp32_mquickjs_wifi_smartconfig_service());run_worker();}
static esp32_mquickjs_wifi_smartconfig_session_t *create(void){esp32_mquickjs_wifi_smartconfig_session_t *s=NULL;assert(esp32_mquickjs_wifi_smartconfig_session_create(&options,true,10000,&s)==0 && s);return s;}
static void activate(esp32_mquickjs_wifi_smartconfig_session_t *s){assert(esp32_mquickjs_wifi_smartconfig_session_activate(s)==0);}
static void stop(esp32_mquickjs_wifi_smartconfig_session_t *s){esp32_mquickjs_wifi_smartconfig_session_close(s,false);if(!s->status.retired)tick();assert(s->status.retired && !s_sc_active);esp32_mquickjs_wifi_smartconfig_session_release(s);assert(!s_sc_handles);}
static void cancel_during_transfer(void){assert(current->status.worker_busy);esp32_mquickjs_wifi_smartconfig_session_close(current,false);}
static void reset(void){assert(!s_sc_active && !s_sc_handles && !scheduled);start_error=poll_error=finish_error=0;alloc_fail=queue_fail=partial_start=capture_ready=hold_close=radio_consumed=ack_busy=false;during_transfer=NULL;memset(&radio_state,0,sizeof(radio_state));memset(&connection_state,0,sizeof(connection_state));connection_begins=connection_ends=connection_keeps=0;connection_begin_error=connection_end_error=0;connection_partial=valid_password=false;assert(esp32_mquickjs_wifi_smartconfig_open_runtime()==0);}
static void pump(void){now+=200000;(void)esp32_mquickjs_wifi_smartconfig_service();if(scheduled)run_worker();}
static esp32_mquickjs_wifi_smartconfig_session_t *automatic(void){
 esp32_mquickjs_wifi_smartconfig_session_t *s=create();
 esp32_mquickjs_wifi_smartconfig_connection_options_t c={.timeout_ms=30000,.minimum_auth=WIFI_AUTH_WPA2_PSK};
 assert(!esp32_mquickjs_wifi_smartconfig_session_configure_connection(s,&c));activate(s);capture_ready=true;pump();return s;
}
int main(void){
 reset();alloc_fail=true;esp32_mquickjs_wifi_smartconfig_session_t *s=NULL;
 assert(esp32_mquickjs_wifi_smartconfig_session_create(&options,true,1000,&s)==ESP_ERR_NO_MEM && !s_sc_handles);alloc_fail=false;
 esp32_mquickjs_wifi_smartconfig_session_t *held[4];for(unsigned i=0;i<4;i++)held[i]=create();
 assert(esp32_mquickjs_wifi_smartconfig_session_create(&options,true,1000,&s)==ESP_ERR_NO_MEM);
 for(unsigned i=0;i<4;i++)esp32_mquickjs_wifi_smartconfig_session_release(held[i]);assert(!s_sc_handles);
 uint8_t key[16];memset(key,'A',16);options.type=SC_TYPE_ESPTOUCH_V2;options.key=key;options.key_length=16;s=create();memset(key,'B',16);assert(s->key[0]=='A');
 activate(s);assert(esp32_mquickjs_wifi_smartconfig_session_activate(s)==ESP_ERR_INVALID_STATE);
 assert(esp32_mquickjs_wifi_smartconfig_service());esp32_mquickjs_wifi_smartconfig_session_close(s,false);run_worker();
 assert(!starts && s->status.retired && !s_sc_active);for(unsigned i=0;i<16;i++)assert(!s->key[i]);esp32_mquickjs_wifi_smartconfig_session_release(s);
 options.type=SC_TYPE_ESPTOUCH;options.key=NULL;options.key_length=0;
 reset();s=create();activate(s);queue_fail=true;assert(!esp32_mquickjs_wifi_smartconfig_service() && !s->status.worker_busy && s->status.cleanup_error==ESP_ERR_NO_MEM);
 queue_fail=false;tick();assert(starts==1 && s_sc_active==s);
 capture_ready=true;tick();assert(s->status.credentials_ready && finishes==1 && copies==1 && commits==1 && radio_consumed);
 esp32_mquickjs_wifi_smartconfig_credentials_t credential;assert(esp32_mquickjs_wifi_smartconfig_session_credentials(s,&credential,false)==0 && credential.network.password[0]=='P');
 assert(esp32_mquickjs_wifi_smartconfig_session_credentials(s,&credential,false)==0); /* Conversion retry. */
 assert(esp32_mquickjs_wifi_smartconfig_session_credentials(s,NULL,true)==0 && s->credentials.network.password[0]==0);
 assert(esp32_mquickjs_wifi_smartconfig_session_credentials(s,&credential,false)==ESP_ERR_INVALID_STATE);
 assert(esp32_mquickjs_wifi_smartconfig_session_ack(s)==0);tick();assert(acks==1 && s->status.native.decoder.ack_busy);
 ack_busy=false;tick();assert(!s->status.native.decoder.ack_busy);assert(!esp32_mquickjs_wifi_smartconfig_service());stop(s);
 reset();s=create();activate(s);tick();hold_close=true;esp32_mquickjs_wifi_smartconfig_session_close(s,true);tick();assert(s->status.timed_out && s->status.error==ESP_ERR_TIMEOUT && !s->status.retired);
 assert(!esp32_mquickjs_wifi_smartconfig_prepare_runtime_destroy());assert(!esp32_mquickjs_wifi_smartconfig_service()); /* Cleanup worker already queued. */
 esp32_mquickjs_wifi_smartconfig_session_release(s);run_worker();assert(s_sc_active && s_sc_handles==1);
 hold_close=false;assert(!esp32_mquickjs_wifi_smartconfig_prepare_runtime_destroy());run_worker();assert(esp32_mquickjs_wifi_smartconfig_prepare_runtime_destroy());assert(!s_sc_handles);
 reset();s=create();activate(s);start_error=ESP_FAIL;partial_start=true;hold_close=true;tick();assert(s->status.error==ESP_FAIL && s->operation.identity && !s->status.retired);
 hold_close=false;tick();assert(s->status.retired);esp32_mquickjs_wifi_smartconfig_session_release(s);
 reset();s=create();activate(s);current=s;capture_ready=true;during_transfer=cancel_during_transfer;tick();assert(s->status.retired && !s->status.credentials_ready && !s->credentials.network.password[0]);esp32_mquickjs_wifi_smartconfig_session_release(s);
 reset();s=create();activate(s);s->deadline_us=now;tick();assert(s->status.retired && s->status.timed_out);esp32_mquickjs_wifi_smartconfig_session_release(s);
 reset();s=create();activate(s);tick();esp32_mquickjs_wifi_smartconfig_session_release(s);assert(s_sc_active && s_sc_active->status.closing);tick();assert(!s_sc_active && !s_sc_handles);
 /* Invalid credentials must retire even though no Station generation was acquired. */
 reset();s=automatic();pump();assert(s->status.closing && s->status.error==ESP_ERR_INVALID_ARG && !connection_begins);
 pump();assert(s->status.retired && !s_sc_active);esp32_mquickjs_wifi_smartconfig_session_release(s);
 /* Retry a pending capture/IP barrier without duplicating a native connection. */
 reset();valid_password=true;s=automatic();connection_begin_error=ESP_ERR_NOT_FINISHED;pump();assert(!s->status.closing && !s->status.connection_generation);
 connection_begin_error=0;pump();assert(connection_begins==2 && s->status.connection_started);
 assert(esp32_mquickjs_wifi_smartconfig_session_credentials(s,&credential,false)==ESP_ERR_INVALID_STATE);
 connection_state.terminal=1;connection_state.connected=true;pump();assert(s->ack_submitted && ack_busy);
 ack_busy=false;radio_state.decoder.ack_completed=true;pump();assert(!s->status.connection_transferred);
 queue_fail=true;pump();assert(s->status.connection_transferred && !s->status.completed && connection_keeps==1);
 queue_fail=false;pump();assert(s->status.completed && s->status.retired && !s_sc_active);
 assert(!esp32_mquickjs_wifi_smartconfig_session_credentials(s,&credential,false) && credential.network.password[0]=='A');
 assert(!esp32_mquickjs_wifi_smartconfig_session_credentials(s,NULL,true));stop(s);assert(connection_keeps==1 && connection_state.connected);
 /* Submission errors retaining a generation must drain it before Radio close. */
 reset();valid_password=true;s=automatic();connection_begin_error=ESP_FAIL;connection_partial=true;connection_end_error=ESP_ERR_NOT_FINISHED;pump();
 assert(s->status.closing && s->status.connection_generation && !s->status.retired);unsigned old_closes=closes;
 pump();assert(closes==old_closes && connection_ends==2);
 connection_end_error=0;pump();assert(s->status.retired && !connection_keeps);esp32_mquickjs_wifi_smartconfig_session_release(s);
 /* Acquisition deadline still applies after credentials, before ACK handoff. */
 reset();valid_password=true;s=automatic();pump();s->deadline_us=now;pump();
 assert(s->status.timed_out && s->status.retired && connection_ends==1 && !connection_keeps);esp32_mquickjs_wifi_smartconfig_session_release(s);
 assert(allocs==frees && !s_sc_workers);
 return 0;
}
