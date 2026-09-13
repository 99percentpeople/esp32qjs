
static unsigned locks,allocs,frees,starts,finishes,closes,copies,commits,acks;
static bool alloc_fail,queue_fail,partial_start,capture_ready,hold_close,radio_consumed,ack_busy;
static int start_error,poll_error,finish_error;
static int64_t now=100;
static void (*scheduled)(void *),(*during_transfer)(void);
static void *scheduled_arg;
static esp32_mquickjs_wifi_smartconfig_radio_status_t radio_state;
static esp32_mquickjs_wifi_smartconfig_connection_status_t connection_state;
static unsigned connection_begins,connection_ends,connection_keeps;
static int connection_begin_error,connection_end_error;
static bool connection_partial,valid_password;
#define portENTER_CRITICAL(p) do{(void)(p);assert(!locks);locks=1;}while(0)
#define portEXIT_CRITICAL(p) do{(void)(p);assert(locks);locks=0;}while(0)
static void *heap_caps_calloc(size_t n,size_t size,int caps){assert(!locks && caps==1);if(alloc_fail)return NULL;allocs++;return calloc(n,size);}
static void heap_caps_free(void *p){assert(!locks && p);frees++;free(p);}
static void esp32_mquickjs_wireless_secure_zero(void *p,size_t n){volatile uint8_t *v=p;while(n--)*v++=0;}
static int64_t esp_timer_get_time(void){return now;}
static bool esp32_mquickjs_submit_background_worker(void (*fn)(void *),void *arg){assert(!locks && !scheduled);if(queue_fail)return false;scheduled=fn;scheduled_arg=arg;return true;}
esp_err_t esp32_mquickjs_wifi_smartconfig_capture_owners(esp32_mquickjs_wifi_radio_lease_t owners[3]){assert(!locks);for(unsigned i=0;i<3;i++)owners[i]=(esp32_mquickjs_wifi_radio_lease_t){.identity=i+1,.generation=5,.client=i,.acquired=true};return 0;}
esp_err_t esp32_mquickjs_wifi_smartconfig_decoder_validate_options(const esp32_mquickjs_wifi_smartconfig_decoder_options_t *o){return o && o->channel_timeout_s>=15 ? ESP_OK : ESP_ERR_INVALID_ARG;}
esp_err_t esp32_mquickjs_wifi_radio_smartconfig_begin(const esp32_mquickjs_wifi_radio_lease_t owners[3],bool allow_ap,const esp32_mquickjs_wifi_smartconfig_decoder_options_t *options,esp32_mquickjs_wifi_radio_operation_t *token,esp32_mquickjs_wifi_smartconfig_radio_status_t *status){
 assert(!locks && allow_ap && owners[1].identity==2 && options->channel_timeout_s==60);starts++;
 if(options->key_length)assert(options->key_length==16 && options->key[0]=='A');
 memset(&radio_state,0,sizeof(radio_state));radio_consumed=false;
 if(!start_error || partial_start)*token=(esp32_mquickjs_wifi_radio_operation_t){.generation=5,.lease_identity=2,.identity=starts,.kind=3};
 radio_state.operation=*token;radio_state.decoder.started=token->identity!=0;radio_state.stage="native-start";*status=radio_state;return start_error;
}
esp_err_t esp32_mquickjs_wifi_radio_smartconfig_status(const esp32_mquickjs_wifi_radio_operation_t *token,esp32_mquickjs_wifi_smartconfig_radio_status_t *status){assert(!locks && token->identity);radio_state.events.credentials_received=capture_ready;radio_state.events.credentials_consumed=radio_consumed;radio_state.decoder.error=poll_error;radio_state.decoder.ack_busy=ack_busy;*status=radio_state;return 0;}
esp_err_t esp32_mquickjs_wifi_radio_smartconfig_finish_capture(const esp32_mquickjs_wifi_radio_operation_t *token,esp32_mquickjs_wifi_smartconfig_radio_status_t *status){assert(!locks && token->identity && capture_ready);finishes++;radio_state.decoder.capture_stopped=true;radio_state.channel_restored=true;*status=radio_state;return finish_error;}
esp_err_t esp32_mquickjs_wifi_radio_smartconfig_credentials(const esp32_mquickjs_wifi_radio_operation_t *token,esp32_mquickjs_wifi_smartconfig_credentials_t *out,bool commit){assert(!locks && token->identity && radio_state.channel_restored && !radio_consumed);if(commit){assert(!out);commits++;radio_consumed=true;if(during_transfer)during_transfer();}else{assert(out);copies++;memset(out,0,sizeof(*out));memset(out->network.ssid,'S',32);memset(out->network.password,valid_password?'A':'P',64);}return 0;}
esp_err_t esp32_mquickjs_wifi_radio_smartconfig_ack(const esp32_mquickjs_wifi_radio_operation_t *token,esp32_mquickjs_wifi_smartconfig_radio_status_t *status){assert(!locks && token->identity && radio_consumed);acks++;radio_state.decoder.ack_identity=31;radio_state.decoder.ack_busy=ack_busy=true;*status=radio_state;return 0;}
esp_err_t esp32_mquickjs_wifi_radio_smartconfig_close(esp32_mquickjs_wifi_radio_operation_t *token,esp32_mquickjs_wifi_smartconfig_radio_status_t *status){assert(!locks && token->identity);closes++;radio_state.stage="native-close";radio_state.decoder.closing=true;*status=radio_state;if(hold_close)return ESP_ERR_NOT_FINISHED;memset(token,0,sizeof(*token));return 0;}
esp_err_t esp32_mquickjs_wifi_smartconfig_connect_begin(const esp32_mquickjs_wifi_radio_operation_t *owner,const wifi_config_t *config,uint32_t timeout,uint32_t *generation){
 assert(!locks && owner->identity && config->sta.pmf_cfg.capable && timeout && !*generation);
 connection_begins++;if(!connection_begin_error || connection_partial)*generation=connection_state.generation=71;return connection_begin_error;
}
esp_err_t esp32_mquickjs_wifi_smartconfig_connect_status(const esp32_mquickjs_wifi_radio_operation_t *owner,uint32_t generation,esp32_mquickjs_wifi_smartconfig_connection_status_t *status){
 assert(!locks && owner->identity && generation==connection_state.generation);*status=connection_state;return 0;
}
esp_err_t esp32_mquickjs_wifi_smartconfig_connect_end(const esp32_mquickjs_wifi_radio_operation_t *owner,uint32_t generation,bool keep){
 assert(!locks && owner->identity && generation==connection_state.generation);connection_ends++;
 if(connection_end_error)return connection_end_error;
 if(keep){assert(connection_state.connected && connection_state.terminal==1);connection_keeps++;}
 else connection_state.connected=false;
 connection_state.generation=0;return 0;
}
