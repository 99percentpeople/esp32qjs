
static esp32_mquickjs_wifi_smartconfig_decoder_options_t options={.type=SC_TYPE_ESPTOUCH,.channel_timeout_s=60};
static esp32_mquickjs_wifi_smartconfig_decoder_t *create(void){esp32_mquickjs_wifi_smartconfig_decoder_t *d=NULL;assert(esp32_mquickjs_wifi_smartconfig_decoder_create(2,&options,&d)==0 && d);return d;}
static void release(esp32_mquickjs_wifi_smartconfig_decoder_t **d){assert(esp32_mquickjs_wifi_smartconfig_decoder_close(*d)==0);for(size_t i=0;i<sizeof((*d)->key);i++)assert(!(*d)->key[i]);assert(esp32_mquickjs_wifi_smartconfig_decoder_release(d)==0 && !*d && !s_sc_decoder_reserved);}
int main(void){
 esp32_mquickjs_wifi_smartconfig_decoder_t *d=NULL,*other=NULL;
 options.channel_timeout_s=14;assert(esp32_mquickjs_wifi_smartconfig_decoder_create(2,&options,&d)==ESP_ERR_INVALID_ARG && !allocs);options.channel_timeout_s=60;
 uint8_t key[16]={0};options.type=SC_TYPE_ESPTOUCH_V2;options.key=key;options.key_length=16;
 assert(esp32_mquickjs_wifi_smartconfig_decoder_create(2,&options,&d)==ESP_ERR_INVALID_ARG && !allocs);
 memset(key,'A',16);d=create();assert(d->config.esp_touch_v2_key==d->key && !memcmp(d->key,key,16));memset(key,'B',16);assert(d->key[0]=='A');release(&d);
 options.type=SC_TYPE_ESPTOUCH;options.key=NULL;options.key_length=0;
 alloc_error=true;assert(esp32_mquickjs_wifi_smartconfig_decoder_create(2,&options,&d)==ESP_ERR_NO_MEM && !s_sc_decoder_reserved);alloc_error=false;
 d=create();assert(esp32_mquickjs_wifi_smartconfig_decoder_create(2,&options,&other)==ESP_ERR_INVALID_STATE && !other);
 native_status=1;assert(esp32_mquickjs_wifi_smartconfig_decoder_start(d)==ESP_ERR_INVALID_STATE && !event_owned && !start_calls);
 assert(esp32_mquickjs_wifi_smartconfig_decoder_release(&d)==ESP_ERR_INVALID_STATE);release(&d);assert(!stop_calls && native_status==1);native_status=0;
 d=create();ipc_error=ESP_ERR_NO_MEM;assert(esp32_mquickjs_wifi_smartconfig_decoder_start(d)==ESP_ERR_NO_MEM && !d->handoff_unknown);ipc_error=0;release(&d);
 d=create();assert(esp32_mquickjs_wifi_smartconfig_decoder_start(d)==0 && d->started);
 assert(esp32_mquickjs_wifi_smartconfig_decoder_start(d)==ESP_ERR_INVALID_STATE && start_calls==1);
 esp32_mquickjs_wifi_smartconfig_decoder_status_t status;
 timer_error=ESP_FAIL;assert(esp32_mquickjs_wifi_smartconfig_decoder_status(d,&status)==0 && status.error==ESP_FAIL);timer_error=0;
 assert(esp32_mquickjs_wifi_smartconfig_decoder_ack(d)==ESP_ERR_INVALID_STATE && !d->ack_attempted);
 drain_error=ESP_ERR_NOT_FINISHED;assert(esp32_mquickjs_wifi_smartconfig_decoder_finish_capture(d)==ESP_ERR_NOT_FINISHED && !stop_calls && !event_closed);
 drain_error=0;scan_error=ESP_FAIL;assert(esp32_mquickjs_wifi_smartconfig_decoder_finish_capture(d)==ESP_FAIL && !stop_calls);
 scan_error=0;rx_error=ESP_FAIL;assert(esp32_mquickjs_wifi_smartconfig_decoder_finish_capture(d)==ESP_FAIL && !stop_calls);int scans=scan_calls,lists=list_calls;
 rx_error=0;stop_error=ESP_FAIL;assert(esp32_mquickjs_wifi_smartconfig_decoder_finish_capture(d)==ESP_FAIL && stop_calls==1);
 stop_error=0;assert(esp32_mquickjs_wifi_smartconfig_decoder_finish_capture(d)==0 && stop_calls==2 && fence_calls==1 && timer_owned);
 assert(d->capture_stopped && !d->started && !event_closed && event_owned && ack_owner);
 assert(esp32_mquickjs_wifi_smartconfig_decoder_ack(d)==ESP_ERR_NOT_FINISHED && !d->ack_attempted && d->error==0);
 credential_ready=true;esp32_mquickjs_wifi_smartconfig_credentials_t credential;
 assert(esp32_mquickjs_wifi_smartconfig_decoder_credentials(d,&credential,false)==0);
 assert(esp32_mquickjs_wifi_smartconfig_decoder_credentials(d,NULL,true)==0 && !credential_ready);
 assert(esp32_mquickjs_wifi_smartconfig_decoder_ack(d)==0 && d->ack_identity==99);
 assert(esp32_mquickjs_wifi_smartconfig_decoder_ack(d)==ESP_ERR_INVALID_STATE);
 assert(esp32_mquickjs_wifi_smartconfig_decoder_status(d,&status)==0 && status.capture_stopped && status.ack_busy);
 assert(esp32_mquickjs_wifi_smartconfig_decoder_close(d)==ESP_ERR_NOT_FINISHED && event_closed && stop_calls==2);
 assert(scan_calls==scans && list_calls==lists);assert(esp32_mquickjs_wifi_smartconfig_decoder_release(&d)==ESP_ERR_INVALID_STATE);
 ack_busy=false;release(&d);assert(stop_calls==2 && fence_calls==1);
 d=create();start_error=ESP_FAIL;assert(esp32_mquickjs_wifi_smartconfig_decoder_start(d)==ESP_FAIL && d->decoder_owned);start_error=0;release(&d);assert(stop_calls==3);
 /* SDK success after an internal failed allocation must not publish started. */
 d=create();oom_on_start=true;
 assert(esp32_mquickjs_wifi_smartconfig_decoder_start(d)==ESP_ERR_NO_MEM && d->decoder_owned && !d->started);
 assert(esp32_mquickjs_wifi_smartconfig_decoder_status(d,&status)==0 && status.error==ESP_ERR_NO_MEM && !strcmp(status.stage,"smartconfig-allocation"));
 oom_on_start=false;release(&d);
 /* A later native failure is visible before any observer/event queue. */
 d=create();assert(esp32_mquickjs_wifi_smartconfig_decoder_start(d)==0);
 native_allocation_error=ESP_ERR_NO_MEM;
 assert(esp32_mquickjs_wifi_smartconfig_decoder_status(d,&status)==0 && status.error==ESP_ERR_NO_MEM && !strcmp(status.stage,"smartconfig-allocation"));
 release(&d);
 assert(allocs==frees);
 /* Unconfirmed native dispatch retains all storage and blocks reuse/release. */
 d=create();ipc_error=ESP_ERR_TIMEOUT;assert(esp32_mquickjs_wifi_smartconfig_decoder_start(d)==ESP_ERR_TIMEOUT);
 assert(esp32_mquickjs_wifi_smartconfig_decoder_status(d,&status)==0 && status.handoff_unknown && status.token.identity==0);
 assert(esp32_mquickjs_wifi_smartconfig_decoder_close(d)==ESP_ERR_INVALID_STATE);
 assert(esp32_mquickjs_wifi_smartconfig_decoder_release(&d)==ESP_ERR_INVALID_STATE && d);
 assert(esp32_mquickjs_wifi_smartconfig_decoder_create(2,&options,&other)==ESP_ERR_INVALID_STATE);
 return 0;
}
