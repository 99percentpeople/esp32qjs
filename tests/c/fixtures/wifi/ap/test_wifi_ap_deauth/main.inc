
static wifi_ap_deauth_ipc_t *queued;
int esp_wifi_ipc_internal(wifi_ap_deauth_ipc_t *config,bool sync){assert(locks&&!critical&&sync&&config&&config->arg_size==0);ipc_calls++;
 if(defer){assert(!queued);queued=config;return ipc_error;}
 if(ipc_error)return ipc_error;
 if(before_dispatch_replace)current_mac[5]=2;
 on_wifi=true;int e=config->fn(config->arg);on_wifi=false;return e;}
static esp32_mquickjs_wifi_radio_lease_t ap_lease;
static bool requested,unknown;
static const char *stage;
static const uint8_t address[6]={2,0,0,0,0,1};
static int request(void){return esp32_mquickjs_wifi_radio_ap_deauth(&ap_lease,address,&requested,&unknown,&stage);}
static void setup(void){assert(!s_ap_deauth&&!queued&&allocations==frees);reset_vendor();memset(&ap_lease,0,sizeof(ap_lease));
 s_radio.effective_mode=s_radio.event_live=WIFI_MODE_AP;s_radio.next_operation_identity=1;
 wifi_radio_operation_lock();assert(!wifi_radio_acquire_locked(ESP32_MQUICKJS_WIFI_RADIO_CLIENT_WIFI_AP,WIFI_MODE_AP,&ap_lease));wifi_radio_operation_unlock();
 on_wifi=fail_alloc=defer=before_dispatch_replace=false;ipc_error=lookup_error=deauth_error=0;
 ipc_calls=lookups=deauths=0;current_aid=1;memcpy(current_mac,address,6);}
static void late(void){assert(queued&&!locks&&!critical);wifi_ap_deauth_ipc_t *q=queued;queued=NULL;
 on_wifi=true;q->fn(q->arg);on_wifi=false;}
int main(void){setup();
 assert(!request()&&requested&&!unknown&&lookups==1&&deauths==1&&!memcmp(sent_mac,address,6)&&!s_radio.operation.identity);
 setup();before_dispatch_replace=true;assert(!request()&&!requested&&!deauths); /* Old AID now belongs to another MAC. */
 setup();current_aid=3;assert(!request()&&requested&&last_aid==3); /* Look up at execution, never reuse a caller's old AID. */
 setup();current_aid=0;assert(request()==ESP_ERR_INVALID_RESPONSE&&!deauths);
 current_aid=ESP_WIFI_MAX_CONN_NUM+1;assert(request()==ESP_ERR_INVALID_RESPONSE&&!deauths);
 setup();lookup_error=77;assert(request()==77&&!requested&&!deauths&&!strcmp(stage,"deauth-address"));
 setup();deauth_error=88;assert(request()==88&&!requested&&deauths==1&&!strcmp(stage,"deauth-request"));
 setup();fail_alloc=true;assert(request()==ESP_ERR_NO_MEM&&!ipc_calls&&!s_radio.operation.identity);
 setup();s_radio.next_operation_identity=0;assert(request()==ESP_ERR_NO_MEM&&!ipc_calls);
 setup();ap_lease.generation++;assert(request()==ESP_ERR_INVALID_STATE&&!ipc_calls);
 setup();s_radio.lifecycle.identity=999;assert(request()==ESP_ERR_INVALID_STATE&&!ipc_calls);
 setup();ipc_error=ESP_ERR_NO_MEM;assert(request()==ESP_ERR_NO_MEM&&!unknown&&allocations==frees);
 setup();defer=true;ipc_error=77;assert(request()==77&&unknown&&!requested&&s_radio.operation.identity);
 esp32_mquickjs_wifi_radio_operation_t old=s_radio.operation;esp32_mquickjs_wifi_radio_end_operation(&old);
 assert(old.identity&&s_radio.operation.identity);
 wifi_radio_operation_lock();wifi_radio_release_locked(&ap_lease);assert(wifi_radio_lease_valid(&ap_lease));wifi_radio_operation_unlock();
 bool pending=false;assert(!esp32_mquickjs_wifi_radio_ap_deauth_poll(&pending)&&pending&&allocations==frees+1);
 assert(request()==ESP_ERR_INVALID_STATE&&ipc_calls==1);late();
 assert(esp32_mquickjs_wifi_radio_ap_deauth_poll(&pending)&&!pending&&!s_radio.operation.identity&&!s_radio.fault_stage);
 assert(ipc_calls==1&&deauths==1&&allocations==frees);
 assert(!esp32_mquickjs_wifi_radio_ap_deauth_poll(NULL));
 /* A stale token and a late receipt cannot release a later operation or
  * clear a fault from another source. */
 defer=false;ipc_error=0;assert(!request());esp32_mquickjs_wifi_radio_end_operation(&old);assert(!s_radio.operation.identity);
 setup();defer=true;ipc_error=77;assert(request()==77);s_radio.fault_stage="unrelated-fault";late();
 assert(esp32_mquickjs_wifi_radio_ap_deauth_poll(NULL)&&!strcmp(s_radio.fault_stage,"unrelated-fault"));
 assert(!s_ap_deauth&&allocations==frees&&!critical&&!locks);return 0;
}
