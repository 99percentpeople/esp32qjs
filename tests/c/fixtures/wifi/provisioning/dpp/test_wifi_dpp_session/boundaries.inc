
static unsigned locks,allocations,frees,starts,copies,commits,helper_identity;
static bool fail_alloc,fail_queue,radio_active,helper_ready,hold_native,hold_helper,terminal,uri_generated;
static int begin_error,row_error,authentication_error;
static int restore_error,recover_error;
static unsigned recoveries;
static bool native_connection,hold_connection;
static unsigned selections,connections,connection_ends,connection_terminal;
static int64_t now=100;
static size_t allocation_size;
static void (*scheduled)(void*),(*during_copy)(void);
static void *argument;
static esp32_mquickjs_wifi_dpp_radio_status_t radio;
#define portENTER_CRITICAL(p) do{(void)p;assert(!locks);locks++;}while(0)
#define portEXIT_CRITICAL(p) do{(void)p;assert(locks==1);locks--;}while(0)
static void esp32_mquickjs_wireless_secure_zero(void*p,size_t n){volatile uint8_t*q=p;while(n--)*q++=0;}
static void*heap_caps_calloc(size_t n,size_t z,int caps){assert(!locks&&caps==1);if(fail_alloc)return NULL;
 void*p=calloc(n,z);assert(p);allocation_size=n*z;allocations++;return p;}
static void heap_caps_free(void*p){assert(!locks&&p);for(size_t i=0;i<allocation_size;i++)assert(!((uint8_t*)p)[i]);free(p);frees++;}
static int64_t esp_timer_get_time(void){return now;}
static bool esp32_mquickjs_submit_background_worker(void(*fn)(void*),void*p){
 assert(!locks&&!scheduled);if(fail_queue)return false;scheduled=fn;argument=p;return true;}
esp_err_t esp32_mquickjs_wifi_dpp_worker_validate(const esp32_mquickjs_wifi_dpp_worker_options_t*o){
 return o&&!strcmp(o->channels,"6")?ESP_OK:ESP_ERR_INVALID_ARG;}
esp_err_t esp32_mquickjs_wifi_dpp_station_reserve(uint32_t*id,esp32_mquickjs_wifi_radio_lease_t owners[3]){
 assert(!locks&&!*id);if(helper_identity)return ESP_ERR_INVALID_STATE;helper_identity=*id=7;
 for(unsigned i=0;i<3;i++)owners[i]=(esp32_mquickjs_wifi_radio_lease_t){.identity=i+1,.generation=1,.acquired=true};return 0;}
esp_err_t esp32_mquickjs_wifi_dpp_station_drain(uint32_t id){
 assert(!locks&&id==helper_identity);if(radio_active)assert(radio.worker.capture_retired||radio.worker.retired);
 if(hold_helper)return ESP_ERR_NOT_FINISHED;helper_ready=true;return 0;}
esp_err_t esp32_mquickjs_wifi_dpp_station_release(uint32_t*id){assert(!radio_active);
 int e=esp32_mquickjs_wifi_dpp_station_drain(*id);if(!e)helper_identity=*id=0;return e;}
esp_err_t esp32_mquickjs_wifi_radio_dpp_begin(const esp32_mquickjs_wifi_radio_lease_t owners[3],bool allow,
 const esp32_mquickjs_wifi_dpp_worker_options_t*o,esp32_mquickjs_wifi_radio_operation_t*t,esp32_mquickjs_wifi_dpp_radio_status_t*out){
 assert(!locks&&helper_identity&&!t->identity&&owners[1].identity==2&&!allow&&!strcmp(o->channels,"6"));starts++;
 t->identity=13;t->generation=1;radio_active=true;radio=(esp32_mquickjs_wifi_dpp_radio_status_t){.operation=*t};*out=radio;return begin_error;}
esp_err_t esp32_mquickjs_wifi_radio_dpp_status(const esp32_mquickjs_wifi_radio_operation_t*t,esp32_mquickjs_wifi_dpp_radio_status_t*out){
 assert(!locks&&t->identity==13&&radio_active);radio.worker.native.terminal=terminal;
 radio.worker.native.config_count=terminal?2:0;radio.worker.native.uri_length=uri_generated?6:0;
 *out=radio;return 0;}
esp_err_t esp32_mquickjs_wifi_radio_dpp_listen(const esp32_mquickjs_wifi_radio_operation_t*t,esp32_mquickjs_wifi_dpp_radio_status_t*out){
 assert(!locks&&t->identity==13);*out=radio;return uri_generated?0:ESP_ERR_NOT_FINISHED;}
esp_err_t esp32_mquickjs_wifi_radio_dpp_uri(const esp32_mquickjs_wifi_radio_operation_t*t,char*out,size_t cap,bool commit){
 assert(!locks&&t->identity==13&&uri_generated);if(commit){assert(!out&&!cap);radio.worker.native.uri_available=false;}
 else{assert(cap>6);memcpy(out,"DPP:X;",7);}return 0;}
esp_err_t esp32_mquickjs_wifi_radio_dpp_finish_capture(const esp32_mquickjs_wifi_radio_operation_t*t,esp32_mquickjs_wifi_dpp_radio_status_t*out){
 assert(!locks&&t->identity==13&&terminal);if(hold_native)return ESP_ERR_NOT_FINISHED;
 radio.worker.capture_retired=true;radio.channel_restored=true;*out=radio;return 0;}
esp_err_t esp32_mquickjs_wifi_radio_dpp_config(const esp32_mquickjs_wifi_radio_operation_t*t,unsigned i,esp_dpp_config_data_t*out){
 assert(!locks&&t->identity==13&&radio.worker.capture_retired&&helper_ready&&i<2);copies++;
 if(row_error&&i==1)return row_error;memset(out,0,sizeof(*out));out->ssid_len=1;out->ssid[0]='A'+i;out->key[0]=i+1;
 if(during_copy){void(*fn)(void)=during_copy;during_copy=NULL;fn();}return 0;}
esp_err_t esp32_mquickjs_wifi_radio_dpp_configs_commit(const esp32_mquickjs_wifi_radio_operation_t*t){assert(!locks&&t->identity==13);commits++;return 0;}
esp_err_t esp32_mquickjs_wifi_radio_dpp_prepare_close(const esp32_mquickjs_wifi_radio_operation_t*t,esp32_mquickjs_wifi_dpp_radio_status_t*out){
 assert(!locks&&t->identity==13&&!native_connection);if(hold_native)return ESP_ERR_NOT_FINISHED;
 radio.worker.retired=true;
 if(restore_error){radio.restore_start_failed=true;radio.restore_start_error=restore_error;
  radio.restore_recovery_pending=false;*out=radio;return restore_error;}
 radio.restore_start_failed=radio.restore_recovery_pending=false;radio.restore_start_error=0;
 helper_ready=false;*out=radio;return 0;}
esp_err_t esp32_mquickjs_wifi_radio_dpp_recover(const esp32_mquickjs_wifi_radio_operation_t*t,esp32_mquickjs_wifi_dpp_radio_status_t*out){
 assert(!locks&&t->identity==13&&!native_connection&&radio.worker.retired&&radio.restore_start_failed);
 recoveries++;if(recover_error){*out=radio;return recover_error;}
 radio.restore_recovery_pending=true;radio.restore_recovery_attempts++;
 restore_error=0;*out=radio;return 0;}
esp_err_t esp32_mquickjs_wifi_radio_dpp_close(esp32_mquickjs_wifi_radio_operation_t*t,esp32_mquickjs_wifi_dpp_radio_status_t*out){
 assert(!locks&&t->identity==13&&radio.worker.retired&&helper_ready);*out=radio;radio_active=false;t->identity=0;return 0;}
esp_err_t esp32_mquickjs_wifi_dpp_connection_prepare(const esp_dpp_config_data_t*r,esp32_mquickjs_wifi_dpp_auth_t requested,
 esp32_mquickjs_wifi_dpp_auth_t*selected,wifi_config_t*config){if(!r||!r->ssid_len)return ESP_ERR_INVALID_ARG;
 *selected=requested?requested:ESP32_MQUICKJS_DPP_AUTH_CONNECTOR;config->unused=1;return 0;}
esp_err_t esp32_mquickjs_wifi_radio_dpp_select(const esp32_mquickjs_wifi_radio_operation_t*t,const esp_dpp_config_data_t*r,
 esp32_mquickjs_wifi_dpp_auth_t requested,bool allow_ap_restart,esp32_mquickjs_wifi_dpp_auth_t*selected,wifi_config_t*config,
 esp32_mquickjs_wifi_dpp_radio_status_t*out){assert(!locks&&radio_active&&helper_ready&&t->identity==13&&!allow_ap_restart);
 int e=esp32_mquickjs_wifi_dpp_connection_prepare(r,requested,selected,config);if(e)return e;
 selections++;terminal=false;radio.worker.capture_retired=false;radio.worker.connection_selected=true;
 radio.worker.native.identity=20+selections;*out=radio;return 0;}
esp_err_t esp32_mquickjs_wifi_dpp_connect_begin(uint32_t capture,const esp32_mquickjs_wifi_radio_operation_t*t,
 const wifi_config_t*config,uint32_t timeout,uint32_t*generation){assert(!locks&&capture==helper_identity&&t->identity==13&&!native_connection);
 assert(config->unused==1&&timeout&&!*generation);*generation=23;connections++;native_connection=true;connection_terminal=0;return 0;}
esp_err_t esp32_mquickjs_wifi_dpp_connect_status(const esp32_mquickjs_wifi_radio_operation_t*t,uint32_t generation,
 esp32_mquickjs_wifi_dpp_connection_status_t*out){assert(!locks&&t->identity==13&&generation==23&&native_connection);
 *out=(esp32_mquickjs_wifi_dpp_connection_status_t){.generation=23,.terminal=connection_terminal,
 .connected=connection_terminal==ESP32_MQUICKJS_WIFI_CONNECT_EVENT_KIND_SUCCESS,
 .link={.valid=true,.ssid={'A'},.ssid_len=1,.bssid={2,3,4,5,6,7},.channel=6,.aid=1}};return 0;}
esp_err_t esp32_mquickjs_wifi_radio_dpp_check_connection(const esp32_mquickjs_wifi_radio_operation_t*t,
 esp32_mquickjs_wifi_dpp_auth_t auth,const uint8_t bssid[6]){assert(!locks&&t->identity==13&&native_connection&&auth==ESP32_MQUICKJS_DPP_AUTH_CONNECTOR&&bssid[0]==2);return authentication_error;}
esp_err_t esp32_mquickjs_wifi_dpp_connect_end(const esp32_mquickjs_wifi_radio_operation_t*t,uint32_t generation){
 assert(!locks&&t->identity==13&&generation==23&&native_connection);if(hold_connection)return ESP_ERR_NOT_FINISHED;
 native_connection=false;connection_ends++;return 0;}

