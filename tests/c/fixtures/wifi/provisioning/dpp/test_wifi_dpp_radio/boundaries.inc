
struct esp32_mquickjs_wifi_dpp_worker {esp32_mquickjs_wifi_dpp_worker_status_t status;};
static struct esp32_mquickjs_wifi_dpp_worker worker;
static unsigned worker_live,prepares,channel_writes,closes,ap_info_reads;
static int prepare_error,close_error,channel_error;
static bool unknown,associated;
static wifi_config_t station_config;
static wifi_storage_t storage;
static unsigned config_writes,storage_writes;
static int config_error,storage_error;
static bool native_started=true,hold_stop,hold_start;
static unsigned stops,starts,pmf_writes,policy_writes;
static int pmf_error,start_error;
static int8_t power=60;
static wifi_ps_type_t power_save=WIFI_PS_MIN_MODEM;
static uint16_t inactive[2]={60,300};
static struct {unsigned pending;}s_inactive_history;
static uint8_t channel=6;
static int esp_wifi_sta_get_ap_info(wifi_ap_record_t*out){++ap_info_reads;assert(locks&&!critical);memset(out,0,sizeof(*out));
 return !native_started?ESP_ERR_WIFI_NOT_STARTED:associated?ESP_OK:ESP_ERR_WIFI_NOT_CONNECT;}
static int esp_wifi_get_config(wifi_interface_t interface,wifi_config_t*out){assert(locks&&!critical&&interface==WIFI_IF_STA);*out=station_config;return 0;}
static int (esp_wifi_set_config)(wifi_interface_t interface,wifi_config_t*config){assert(locks&&!critical&&interface==WIFI_IF_STA&&storage==WIFI_STORAGE_RAM);
 config_writes++;if(config_error)return config_error;station_config=*config;station_config.sta.pmf_cfg.capable=true;return 0;}
static int (esp_wifi_set_storage)(wifi_storage_t value){assert(locks&&!critical);storage_writes++;if(storage_error)return storage_error;storage=value;return 0;}
static int (esp_wifi_disable_pmf_config)(wifi_interface_t interface){assert(locks&&!critical&&!native_started&&interface==WIFI_IF_STA&&storage==WIFI_STORAGE_RAM);
 pmf_writes++;station_config.sta.pmf_cfg.capable=station_config.sta.pmf_cfg.required=false;return pmf_error;}
static int esp_wifi_get_ps(wifi_ps_type_t*out){assert(locks&&!critical);*out=power_save;return 0;}
static int (esp_wifi_set_ps)(wifi_ps_type_t value){assert(locks&&!critical&&storage==WIFI_STORAGE_RAM);power_save=value;policy_writes++;return 0;}
static int esp_wifi_get_max_tx_power(int8_t*out){assert(locks&&!critical&&native_started);*out=power;return 0;}
static int (esp_wifi_set_max_tx_power)(int8_t value){assert(locks&&!critical&&native_started&&storage==WIFI_STORAGE_RAM);power=value;policy_writes++;return 0;}
static int esp_wifi_get_inactive_time(wifi_interface_t interface,uint16_t*out){assert(locks&&!critical&&native_started);*out=inactive[interface==WIFI_IF_AP];return 0;}
static int (esp_wifi_set_inactive_time)(wifi_interface_t interface,uint16_t value){assert(locks&&!critical&&native_started&&storage==WIFI_STORAGE_RAM);
 inactive[interface==WIFI_IF_AP]=value;policy_writes++;return 0;}
static void wifi_radio_inactive_history_record(unsigned i,uint16_t value,int error){assert(locks&&!critical&&i<2);(void)value;(void)error;}
static void wifi_radio_set_state(esp32_mquickjs_wifi_radio_driver_state_t state){assert(locks&&!critical);s_radio.driver_state=state;}
/* Unrelated recovery owners cannot grant DPP admission. STOP itself and the
 * fault/cleanup ledger below are production functions, not test state machines. */
typedef struct {unsigned generation,identity;} esp32_mquickjs_wifi_action_token_t;
static struct {esp32_mquickjs_wifi_radio_lease_t lease;struct {unsigned generation,identity;bool dispatching,cancel_busy;}lane;}s_action;
static struct {bool stopped;}s_raw_tx_recovery;
static bool wifi_radio_raw_tx_recovery_owner_locked(const esp32_mquickjs_wifi_radio_lease_t*t){(void)t;return false;}
static bool wifi_radio_raw_tx_recovery_exact_locked(const esp32_mquickjs_wifi_radio_lifecycle_t*t){(void)t;return false;}
static bool wifi_radio_action_recovery_exact_locked(const esp32_mquickjs_wifi_radio_lifecycle_t*t){(void)t;return false;}
static bool wifi_radio_action_exact_locked(const esp32_mquickjs_wifi_action_token_t*t){(void)t;return false;}
static void wifi_radio_capture_stop_snapshot_locked(void){assert(locks&&!critical);}
static int esp_wifi_stop(void){assert(locks&&!critical);stops++;native_started=false;return 0;}
static int wifi_radio_begin_events(unsigned phase,wifi_mode_t mode){assert(locks&&!critical&&
 (phase==RADIO_EVENTS_START||phase==RADIO_EVENTS_STOP)&&
 mode==(phase==RADIO_EVENTS_STOP?s_radio.event_live:s_radio.effective_mode));
 s_radio.event_phase=phase;return 0;}
static int wifi_radio_wait_events(void){assert(!"DPP worker must not touch the JS runtime");return ESP_ERR_INVALID_STATE;}
static int wifi_radio_wait_events_inner(void *runtime){assert(!runtime&&locks&&!critical&&
 (s_radio.event_phase==RADIO_EVENTS_START||s_radio.event_phase==RADIO_EVENTS_STOP));
 if(s_radio.event_phase==RADIO_EVENTS_START?hold_start:hold_stop)return ESP_ERR_NOT_FINISHED;
 s_radio.event_live=s_radio.event_phase==RADIO_EVENTS_START?s_radio.effective_mode:WIFI_MODE_NULL;
 s_radio.event_phase=RADIO_EVENTS_IDLE;return 0;}
static int (esp_wifi_start)(void){assert(locks&&!critical&&!native_started&&!station_config.sta.pmf_cfg.capable);
 starts++;if(start_error)return start_error;native_started=true;power=8;power_save=WIFI_PS_NONE;inactive[0]=3;inactive[1]=10;channel=1;return 0;}
static int wifi_radio_get_channel_locked(uint8_t*p,wifi_second_chan_t*s,uint32_t*g){assert(locks&&!critical);*p=channel;*s=WIFI_SECOND_CHAN_NONE;*g=1;return 0;}
static int wifi_radio_validate_regulatory_channel(uint8_t p){assert(locks&&!critical);return p==6?0:ESP_ERR_INVALID_ARG;}
static int (esp_wifi_set_channel)(uint8_t p,wifi_second_chan_t s){assert(locks&&!critical&&s==WIFI_SECOND_CHAN_NONE);channel_writes++;
 if(channel_error)return channel_error;channel=p;return 0;}
esp_err_t esp32_mquickjs_wifi_dpp_worker_create(const esp32_mquickjs_wifi_dpp_worker_options_t*o,esp32_mquickjs_wifi_dpp_worker_t**out){
 assert(!locks&&!critical&&!*out&&!worker_live);if(!o->channels[0])return ESP_ERR_INVALID_ARG;
 worker_live++;memset(&worker,0,sizeof(worker));*out=&worker;return 0;}
esp_err_t esp32_mquickjs_wifi_dpp_worker_prepare(esp32_mquickjs_wifi_dpp_worker_t*w){assert(locks&&!critical&&w==&worker);prepares++;return prepare_error;}
esp_err_t esp32_mquickjs_wifi_dpp_worker_listen(esp32_mquickjs_wifi_dpp_worker_t*w){assert(locks&&!critical&&w==&worker);channel=11;return 0;}
esp_err_t esp32_mquickjs_wifi_dpp_worker_status(esp32_mquickjs_wifi_dpp_worker_t*w,esp32_mquickjs_wifi_dpp_worker_status_t*out){
 assert(locks&&!critical&&w==&worker);*out=w->status;out->handoff_unknown=unknown;return 0;}
esp_err_t esp32_mquickjs_wifi_dpp_worker_finish_capture(esp32_mquickjs_wifi_dpp_worker_t*w){assert(locks&&!critical&&w==&worker);
 if(close_error)return close_error;w->status.capture_retired=true;return 0;}
esp_err_t esp32_mquickjs_wifi_dpp_worker_close(esp32_mquickjs_wifi_dpp_worker_t*w){assert(locks&&!critical&&w==&worker);closes++;
 if(close_error)return close_error;w->status.capture_retired=w->status.retired=true;return 0;}
esp_err_t esp32_mquickjs_wifi_dpp_worker_release(esp32_mquickjs_wifi_dpp_worker_t**w){assert(locks&&!critical&&*w==&worker&&worker.status.retired);
 worker_live--;*w=NULL;return 0;}
esp_err_t esp32_mquickjs_wifi_dpp_worker_uri(esp32_mquickjs_wifi_dpp_worker_t*w,char*out,size_t cap,bool commit){
 assert(locks&&!critical&&w==&worker);if(!commit){assert(cap>6);memcpy(out,"DPP:X;",7);}return 0;}
esp_err_t esp32_mquickjs_wifi_dpp_worker_config(esp32_mquickjs_wifi_dpp_worker_t*w,unsigned i,esp_dpp_config_data_t*out){
 assert(locks&&!critical&&w==&worker&&w->status.capture_retired&&channel==6&&i==0);memset(out,1,sizeof(*out));return 0;}
esp_err_t esp32_mquickjs_wifi_dpp_worker_configs_commit(esp32_mquickjs_wifi_dpp_worker_t*w){assert(locks&&!critical&&w==&worker&&channel==6);return 0;}
esp_err_t esp32_mquickjs_wifi_dpp_connection_prepare(const esp_dpp_config_data_t*r,esp32_mquickjs_wifi_dpp_auth_t requested,
 esp32_mquickjs_wifi_dpp_auth_t*selected,wifi_config_t*config){if(!r||r->secret[0]!=1)return ESP_ERR_INVALID_ARG;
 memset(config,0,sizeof(*config));memcpy(config->sta.ssid,"chosen",7);config->sta.pmf_cfg.capable=true;
 *selected=requested?requested:ESP32_MQUICKJS_DPP_AUTH_CONNECTOR;return 0;}
esp_err_t esp32_mquickjs_wifi_dpp_worker_select(esp32_mquickjs_wifi_dpp_worker_t*w,const esp_dpp_config_data_t*r,
 esp32_mquickjs_wifi_dpp_auth_t requested,esp32_mquickjs_wifi_dpp_auth_t*selected,wifi_config_t*config){
 assert(locks&&!critical&&w==&worker&&w->status.capture_retired&&storage==WIFI_STORAGE_RAM);
 int e=esp32_mquickjs_wifi_dpp_connection_prepare(r,requested,selected,config);if(e)return e;
 w->status.capture_retired=false;w->status.connection_selected=true;return 0;}
esp_err_t esp32_mquickjs_wifi_dpp_worker_check_connection(esp32_mquickjs_wifi_dpp_worker_t*w,
 esp32_mquickjs_wifi_dpp_auth_t selected,const uint8_t bssid[6]){
 assert(locks&&!critical&&w==&worker&&associated&&selected==ESP32_MQUICKJS_DPP_AUTH_CONNECTOR&&bssid[0]==2);return 0;}
