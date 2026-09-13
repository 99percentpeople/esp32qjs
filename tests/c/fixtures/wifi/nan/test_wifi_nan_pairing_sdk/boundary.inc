
static struct nan_pasn_data *active;
static struct nan_pasn_data *esp_nan_app_get_pasn_data(void){return active;}
static void esp_nan_app_set_pasn_data(struct nan_pasn_data*p){active=p;}
static void *esp32_mquickjs_memory_wireless_calloc(const char*owner,size_t n,size_t z,int cls,int role){
 assert(wifi_task&&!strcmp(owner,"wifi.nan")&&n==1&&!cls&&role==1);
 if(++allocation_calls==fail_allocation)return NULL;
 size_t*p=calloc(1,sizeof(size_t)+z);assert(p);*p=z;++allocations;return p+1;
}
static void esp32_mquickjs_memory_payload_free(void*ptr){
 assert(ptr&&allocations);size_t*p=(size_t*)ptr-1;
 for(size_t i=0;i<*p;i++){assert(!((uint8_t*)ptr)[i]);}
 --allocations;free(p);
}
static struct rsn_pmksa_cache *new_cache(void){
 if(++cache_calls==fail_cache)return NULL;
 ++cache_alive;return calloc(1,sizeof(struct rsn_pmksa_cache));
}
static struct rsn_pmksa_cache *pasn_initiator_pmksa_cache_init(void){return new_cache();}
static struct rsn_pmksa_cache *pasn_responder_pmksa_cache_init(void){return new_cache();}
static void pasn_initiator_pmksa_cache_deinit(struct rsn_pmksa_cache*p){assert(cache_alive);--cache_alive;free(p);}
static void pasn_responder_pmksa_cache_deinit(struct rsn_pmksa_cache*p){assert(cache_alive);--cache_alive;free(p);}
static void wpa_pasn_reset(struct pasn_data*p){(void)p;}
static void pasn_data_deinit(struct pasn_data*p){free(p);}
static int esp_wifi_get_mac(int interface,uint8_t*out){assert(interface==WIFI_IF_NAN);memset(out,2,6);return mac_error;}
static int nan_pasn_esp_send_mgmt(void*c,const uint8_t*d,size_t n,int a,unsigned f,unsigned w){
 (void)c;(void)d;(void)n;(void)a;(void)f;(void)w;return 0;
}
static int eloop_register_timeout(unsigned s,unsigned u,void(*cb)(void*,void*),void*d,void*a){
 (void)s;(void)u;++timer_calls;if(fail_timer)return -1;timer_cb=cb;timer_data=d;timer_arg=a;return 0;
}
static int eloop_cancel_timeout(void(*cb)(void*,void*),void*d,void*a){
 if(timer_cb!=cb||timer_data!=d||(a!=ELOOP_ALL_CTX&&a!=timer_arg))return 0;
 timer_cb=NULL;timer_data=timer_arg=NULL;return 1;
}
static int eloop_register_timeout_blocking(int(*fn)(void*,void*),void*a,void*b){
 assert(!wifi_task);++dispatches;if(dispatch_error)return dispatch_error;
 wifi_task=true;int result=fn(a,b);wifi_task=false;return result;
}
static void nan_pasn_clear_saved_keys(void){}
static int64_t esp_timer_get_time(void){return 1234;}
bool esp32_mquickjs_wifi_nan_pairing_followup_pending(void){return pending_error!=0;}
bool esp32_mquickjs_wifi_nan_tx_pairing_settled(uint32_t id,esp_err_t*error,int64_t*completed){
 (void)id;*error=0;*completed=1234;return !tx_busy;
}
esp_err_t esp32_mquickjs_wifi_nan_pairing_cancel_pending(void){assert(wifi_task);return pending_error;}
bool esp32_mquickjs_wifi_nan_pairing_services(const uint8_t p[6],uint8_t*own,uint8_t*remote){
 assert(wifi_task&&p);if(own)*own=1;if(remote)*remote=2;return !unbound;
}
void esp32_mquickjs_wifi_nan_pairing_release_binding(void){assert(wifi_task);}
bool esp32_mquickjs_wifi_nan_tx_pairing_drained(uint32_t id){(void)id;assert(wifi_task);return !tx_busy;}
static int nan_pasn_initialize(struct nan_pasn_data*p,const uint8_t*peer){
 memcpy(p->pasn_unicast_peer,peer,6);p->pasn=calloc(1,sizeof(struct pasn_data));return protocol_error;
}
static int nan_prepare_pasn_extra_ie(struct nan_pasn_data*p,struct pasn_data*q,void*r,bool verify){
 (void)p;(void)q;(void)r;(void)verify;return protocol_error;
}
int nan_initiate_pasn_auth(struct nan_pasn_data*p,const uint8_t*peer){return nan_pasn_initialize(p,peer);}
int nan_initiate_pasn_verify(struct nan_pasn_data*p,const uint8_t*peer,int r,const uint8_t*b,const uint8_t*s,size_t l){
 (void)r;(void)b;(void)s;(void)l;return nan_pasn_initialize(p,peer);
}
static void nan_pasn_auth_timeout_cancel(struct nan_pasn_data*);
