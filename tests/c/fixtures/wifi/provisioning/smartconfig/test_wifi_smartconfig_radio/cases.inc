
static esp32_mquickjs_wifi_radio_lease_t helper[3];
static esp32_mquickjs_wifi_smartconfig_decoder_options_t options={.type=SC_TYPE_ESPTOUCH,.channel_timeout_s=60};
static void setup(void){
 assert(!s_sc_radio);reset_vendor();memset(helper,0,sizeof(helper));
 s_radio.effective_mode=WIFI_MODE_STA;s_radio.next_operation_identity=1;
 wifi_radio_operation_lock();
 assert(wifi_radio_acquire_locked(ESP32_MQUICKJS_WIFI_RADIO_CLIENT_APPLICATION,WIFI_MODE_STA,&helper[0])==0);
 assert(wifi_radio_acquire_locked(ESP32_MQUICKJS_WIFI_RADIO_CLIENT_WIFI_STA,WIFI_MODE_STA,&helper[1])==0);
 wifi_radio_operation_unlock();channel=6;secondary=WIFI_SECOND_CHAN_NONE;peer_error=ESP_ERR_WIFI_NOT_CONNECT;
 start_error=close_error=channel_error=fence_error=0;mutate=true;construct_fail=false;
 s_sc_fence_posted=s_sc_fence_seen=0;memset(&posted_fence,0,sizeof(posted_fence));
}
static void pinned(void){wifi_radio_operation_lock();for(unsigned i=0;i<3;i++)if(helper[i].acquired){wifi_radio_release_locked(&helper[i]);assert(wifi_radio_lease_valid(&helper[i]));}wifi_radio_operation_unlock();}
static void deliver(void){assert(posted_fence.identity);wifi_radio_smartconfig_observe_fence(&posted_fence);}
static void close_fenced(esp32_mquickjs_wifi_radio_operation_t *token,esp32_mquickjs_wifi_smartconfig_radio_status_t *status){
 assert(esp32_mquickjs_wifi_radio_smartconfig_close(token,status)==ESP_ERR_NOT_FINISHED && token->identity);deliver();
 assert(!esp32_mquickjs_wifi_radio_smartconfig_close(token,status) && !token->identity);
}
int main(void){
 setup();esp32_mquickjs_wifi_radio_operation_t token={0};esp32_mquickjs_wifi_smartconfig_radio_status_t status;
 peer_error=ESP_OK;assert(esp32_mquickjs_wifi_radio_smartconfig_begin(helper,false,&options,&token,&status)==ESP_ERR_INVALID_STATE && !token.identity && !starts);peer_error=ESP_ERR_WIFI_NOT_CONNECT;
 construct_fail=true;assert(esp32_mquickjs_wifi_radio_smartconfig_begin(helper,false,&options,&token,&status)==ESP_ERR_NO_MEM && !token.identity);construct_fail=false;
 assert(esp32_mquickjs_wifi_radio_smartconfig_begin(helper,false,&options,&token,&status)==0 && token.identity && channel==11);
 pinned();esp32_mquickjs_wifi_radio_operation_t stale=token;stale.generation++;
 assert(esp32_mquickjs_wifi_radio_smartconfig_close(&stale,&status)==ESP_ERR_INVALID_STATE && !stops);
 esp32_mquickjs_wifi_radio_end_operation(&token);assert(token.identity && s_radio.operation.identity);
 esp32_mquickjs_wifi_radio_lease_t foreign={0};wifi_radio_operation_lock();assert(wifi_radio_acquire_locked(ESP32_MQUICKJS_WIFI_RADIO_CLIENT_ESPNOW,WIFI_MODE_STA,&foreign)==ESP_ERR_INVALID_STATE);wifi_radio_operation_unlock();
 assert(esp32_mquickjs_wifi_radio_smartconfig_ack(&token,&status)==ESP_ERR_INVALID_STATE);
 channel_error=77;assert(esp32_mquickjs_wifi_radio_smartconfig_finish_capture(&token,&status)==77 && !status.channel_restored);pinned();
 channel_error=0;fence_error=99;
 assert(esp32_mquickjs_wifi_radio_smartconfig_finish_capture(&token,&status)==ESP_ERR_NOT_FINISHED && status.error==99 && !status.event_fenced);pinned();
 fence_error=0;assert(esp32_mquickjs_wifi_radio_smartconfig_finish_capture(&token,&status)==ESP_ERR_NOT_FINISHED && status.channel_restored && channel==6);
 assert(esp32_mquickjs_wifi_radio_smartconfig_connection_begin(&token)==ESP_ERR_INVALID_STATE);
 wifi_radio_smartconfig_observe_fence(&stale);assert(!s_sc_fence_seen);deliver();
 assert(!esp32_mquickjs_wifi_radio_smartconfig_finish_capture(&token,&status) && status.event_fenced);
 assert(!esp32_mquickjs_wifi_radio_smartconfig_connection_begin(&token));
 assert(esp32_mquickjs_wifi_radio_smartconfig_close(&token,&status)==ESP_ERR_NOT_FINISHED && !stops);
 assert(esp32_mquickjs_wifi_radio_smartconfig_connection_end(&stale)==ESP_ERR_INVALID_STATE);
 assert(!esp32_mquickjs_wifi_radio_smartconfig_connection_end(&token));
 assert(esp32_mquickjs_wifi_radio_smartconfig_connection_begin(&token)==ESP_ERR_INVALID_STATE);
 close_error=88;assert(esp32_mquickjs_wifi_radio_smartconfig_close(&token,&status)==88 && token.identity);pinned();
 close_error=0;assert(esp32_mquickjs_wifi_radio_smartconfig_close(&token,&status)==0 && !token.identity && !s_sc_radio && !s_radio.operation.identity && decoder_releases==1);
 setup();start_error=99;mutate=false;unsigned writes=channel_writes;
 assert(esp32_mquickjs_wifi_radio_smartconfig_begin(helper,false,&options,&token,&status)==99 && token.identity);channel=11;
 close_fenced(&token,&status);assert(channel==11 && channel_writes==writes);
 setup();wifi_radio_operation_lock();assert(wifi_radio_acquire_locked(ESP32_MQUICKJS_WIFI_RADIO_CLIENT_WIFI_AP,WIFI_MODE_AP,&helper[2])==0);wifi_radio_operation_unlock();s_radio.effective_mode=WIFI_MODE_APSTA;
 assert(esp32_mquickjs_wifi_radio_smartconfig_begin(helper,false,&options,&token,&status)==ESP_ERR_INVALID_STATE && !token.identity);
 assert(esp32_mquickjs_wifi_radio_smartconfig_begin(helper,true,&options,&token,&status)==0);pinned();close_fenced(&token,&status);
 setup();s_radio.next_operation_identity=0;assert(esp32_mquickjs_wifi_radio_smartconfig_begin(helper,false,&options,&token,&status)==ESP_ERR_NO_MEM && !token.identity);
 return 0;
}
