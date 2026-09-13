
static int64_t native_time=1000,radio_send_duration;
static int radio_send_error;
static unsigned radio_begins,radio_closes,radio_sends;
static bool enqueue_then_close,cancel_during_send;
static esp32_mquickjs_wifi_mesh_session_t*test_session;
static esp32_mquickjs_wifi_mesh_job_t*test_job;
static esp32_mquickjs_wifi_mesh_radio_status_t radio_state;
static test_worker_fn_t pending_worker;static void*pending_opaque;
int64_t esp_timer_get_time(void){return native_time;}
bool esp32_mquickjs_submit_background_worker(test_worker_fn_t fn,void*p){
 assert(!lock_depth&&!pending_worker);pending_worker=fn;pending_opaque=p;return true;
}
static void run_worker(void){test_worker_fn_t fn=pending_worker;void*p=pending_opaque;
 assert(fn);pending_worker=NULL;pending_opaque=NULL;fn(p);}
static void tick(void){native_time+=30000;assert(esp32_mquickjs_wifi_mesh_service());run_worker();}
static esp32_mquickjs_wifi_mesh_job_t*new_job(esp32_mquickjs_wifi_mesh_session_t*s,uint32_t timeout){
 uint8_t bytes[3]={1,2,3};esp32_mquickjs_wifi_mesh_send_t send={.destination=ESP32_MQUICKJS_MESH_TO_ROOT,
 .protocol=MESH_PROTO_BIN,.reliable=true,.bytes=bytes,.length=3};
 esp32_mquickjs_wifi_mesh_job_t*j=NULL;
 assert(!esp32_mquickjs_wifi_mesh_job_create(s,&send,NULL,timeout,&j));memset(bytes,9,sizeof(bytes));return j;
}
static esp32_mquickjs_wifi_mesh_session_t*open_session(void){
 esp32_mquickjs_wifi_mesh_session_t*s=NULL;esp32_mquickjs_wifi_mesh_config_t c=config();
 assert(!esp32_mquickjs_wifi_mesh_open_runtime());
 assert(!esp32_mquickjs_wifi_mesh_session_create(&c,false,5000,&s));
 assert(!esp32_mquickjs_wifi_mesh_session_activate(s));tick();assert(s->status.ready);return s;
}
esp_err_t esp32_mquickjs_wifi_radio_mesh_begin(const esp32_mquickjs_wifi_mesh_config_t*c,bool allow,
 esp32_mquickjs_wifi_mesh_radio_token_t*t,esp32_mquickjs_wifi_mesh_radio_status_t*s){
 (void)allow;assert(!lock_depth&&c->ie_key_length==8);++radio_begins;
 *t=(esp32_mquickjs_wifi_mesh_radio_token_t){9,1};radio_state.token=*t;radio_state.ready=true;
 radio_state.native.started=radio_state.native.root=radio_state.native.native_snapshot_valid=true;
 *s=radio_state;return 0;
}
esp_err_t esp32_mquickjs_wifi_radio_mesh_status(const esp32_mquickjs_wifi_mesh_radio_token_t*t,
 esp32_mquickjs_wifi_mesh_radio_status_t*s){assert(!lock_depth&&t->identity);*s=radio_state;return 0;}
esp_err_t esp32_mquickjs_wifi_radio_mesh_poll(const esp32_mquickjs_wifi_mesh_radio_token_t*t,
 esp32_mquickjs_wifi_mesh_radio_status_t*s){
 assert(!lock_depth&&t->identity);
 if(enqueue_then_close){enqueue_then_close=false;test_job=new_job(test_session,1000);
  assert(!esp32_mquickjs_wifi_mesh_job_submit(test_job));esp32_mquickjs_wifi_mesh_session_close(test_session,false);}
 *s=radio_state;return 0;
}
esp_err_t esp32_mquickjs_wifi_radio_mesh_send(const esp32_mquickjs_wifi_mesh_radio_token_t*t,
 const esp32_mquickjs_wifi_mesh_send_t*send){
 assert(!lock_depth&&t->identity&&send->length==3&&!memcmp(send->bytes,"\1\2\3",3));++radio_sends;
 if(cancel_during_send){
  esp32_mquickjs_wifi_mesh_job_cancel(test_job);esp32_mquickjs_wifi_mesh_job_release(test_job);test_job=NULL;
  assert(!esp32_mquickjs_wifi_mesh_prepare_runtime_destroy());
  assert(s_mesh_jobs==1&&live_allocations==2&&!radio_closes&&!memcmp(send->bytes,"\1\2\3",3));
 }
 native_time+=radio_send_duration;return radio_send_error;
}
esp_err_t esp32_mquickjs_wifi_radio_mesh_control(const esp32_mquickjs_wifi_mesh_radio_token_t*t,
 esp32_mquickjs_wifi_mesh_control_t*c){assert(!lock_depth&&t->identity);c->count=0;return 0;}
esp_err_t esp32_mquickjs_wifi_radio_mesh_receive(const esp32_mquickjs_wifi_mesh_radio_token_t*t,bool ds){
 (void)ds;assert(!lock_depth&&t->identity);return ESP_ERR_NOT_FOUND;}
esp_err_t esp32_mquickjs_wifi_radio_mesh_close(esp32_mquickjs_wifi_mesh_radio_token_t*t,
 esp32_mquickjs_wifi_mesh_radio_status_t*s){
 assert(!lock_depth&&t->identity&&!s_mesh_active->job);++radio_closes;
 radio_state.ready=false;radio_state.native.native_retired=true;radio_state.stopped=radio_state.restored=true;
 *t=(esp32_mquickjs_wifi_mesh_radio_token_t){0};radio_state.token=*t;*s=radio_state;return 0;
}
esp_err_t esp32_mquickjs_wifi_radio_mesh_recover(esp32_mquickjs_wifi_mesh_radio_token_t*t,
 esp32_mquickjs_wifi_mesh_radio_status_t*s){return esp32_mquickjs_wifi_radio_mesh_close(t,s);}
esp_err_t esp32_mquickjs_wifi_radio_mesh_message_copy(const esp32_mquickjs_wifi_mesh_radio_token_t*t,
 bool ds,esp32_mquickjs_wifi_mesh_message_t*m,uint8_t*b,size_t n){
 (void)t;(void)ds;(void)m;(void)b;(void)n;assert(!lock_depth);return ESP_ERR_NOT_FOUND;}
esp_err_t esp32_mquickjs_wifi_radio_mesh_message_commit(const esp32_mquickjs_wifi_mesh_radio_token_t*t,bool ds,uint32_t n){
 (void)t;(void)ds;(void)n;assert(!lock_depth);return ESP_ERR_INVALID_STATE;}
esp_err_t esp32_mquickjs_wifi_radio_mesh_event_copy(const esp32_mquickjs_wifi_mesh_radio_token_t*t,
 esp32_mquickjs_wifi_mesh_notice_t*n){(void)t;(void)n;assert(!lock_depth);return ESP_ERR_NOT_FOUND;}
esp_err_t esp32_mquickjs_wifi_radio_mesh_event_commit(const esp32_mquickjs_wifi_mesh_radio_token_t*t,uint32_t n){
 (void)t;(void)n;assert(!lock_depth);return ESP_ERR_INVALID_STATE;}
