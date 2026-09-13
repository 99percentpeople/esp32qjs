
static unsigned allocations,fail_at,live,cancelled,config_frees,global_resets,method_unregisters;
static bool queue_fail,wait_fail,in_worker,lock_held,dispatch_fail;
static int unregister_error;
static void (*worker)(void *);
static jmp_buf scheduler;
static int task_token;
static void *allocate(size_t n) {if(++allocations==fail_at)return NULL;void *p=calloc(1,n);assert(p);live++;return p;}
static void os_free(void *p){if(p){live--;free(p);}}
#define os_zalloc allocate
static void *os_recursive_mutex_create(void){return allocate(1);}
static void os_mutex_lock(void *m){assert(m && !lock_held);lock_held=true;}
static void os_mutex_unlock(void *m){assert(m && lock_held);lock_held=false;}
static void os_mutex_delete(void *m){assert(!lock_held && !esp32qjs_eap_task_started);os_free(m);}
struct queue {ETSEvent events[16];unsigned read,count;};
static void *os_queue_create(unsigned n,size_t size){assert(n==SIG_WPA2_MAX && size==sizeof(ETSEvent));return allocate(sizeof(struct queue));}
static int os_queue_send(void *p,const void *event,unsigned ticks){(void)ticks;if(queue_fail)return 0;assert(p);struct queue *q=p;assert(q->count<16);q->events[(q->read+q->count++)%16]=*(const ETSEvent *)event;return 1;}
static int os_queue_recv(void *p,void *event,unsigned ticks){assert(in_worker && p && ticks==OS_BLOCK);struct queue *q=p;if(!q->count)longjmp(scheduler,2);*(ETSEvent *)event=q->events[q->read++%16];q->count--;return 1;}
static void os_queue_delete(void *p){assert(p && (in_worker || !esp32qjs_eap_task_started));os_free(p);}
static void *os_semphr_create(unsigned max,unsigned initial){assert(max==1 && !initial);return allocate(sizeof(unsigned));}
static void os_semphr_delete(void *p){assert(!esp32qjs_eap_task_started && !in_worker);os_free(p);}
static void os_semphr_give(void *p){assert(p && *(unsigned *)p==0);*(unsigned *)p=1;}
static void run_worker(void){assert(worker && !in_worker);in_worker=true;if(!setjmp(scheduler))worker(NULL);in_worker=false;}
static int os_semphr_take(void *p,unsigned ticks){assert(p && ticks==OS_BLOCK && !in_worker);if(wait_fail){wait_fail=false;return 0;}if(!*(unsigned *)p)run_worker();assert(*(unsigned *)p==1);*(unsigned *)p=0;return 1;}
static int os_task_create(void (*fn)(void *),const char *name,unsigned stack,void *arg,unsigned priority,void **out){
    (void)name;(void)stack;(void)priority;assert(!arg && s_wpa2_queue && s_wifi_wpa2_sync_sem && esp32qjs_eap_exit_sem);
    if(++allocations==fail_at)return 0;worker=fn;*out=&task_token;return TRUE;
}
static void *os_task_get_current_task(void){return in_worker?&task_token:NULL;}
static unsigned os_task_ms_to_tick(unsigned ms){return ms;}
static void os_task_delete(void *p){assert(!p && in_worker);worker=NULL;longjmp(scheduler,1);}
static void wpa2_set_eap_state(int state){gEapSm->finish_state=state;}
static bool wpa2_is_enabled(void){return s_wpa2_state==WPA2_STATE_ENABLED;}
static int esp_wifi_get_macaddr_internal(int interface,u8 mac[6]){assert(interface==0);memset(mac,0,6);return 0;}
static int eap_peer_blob_init(struct eap_sm *sm){assert(sm);return 0;}
static int eap_peer_config_init(struct eap_sm *sm,const u8 *pw,int n){(void)pw;(void)n;assert(sm);return 0;}
static void eap_peer_config_deinit(struct eap_sm *sm){assert(sm && !esp32qjs_eap_task_started);config_frees++;}
static void eap_peer_blob_deinit(struct eap_sm *sm){assert(sm && !esp32qjs_eap_task_started);}
static void eap_deinit_prev_method(struct eap_sm *sm,const char *reason){(void)reason;assert(sm && !esp32qjs_eap_task_started);}
static void eap_sm_abort(struct eap_sm *sm){assert(sm && !esp32qjs_eap_task_started);}
static void *tls_init(void *p){assert(!p);return allocate(1);}
static void tls_deinit(void *p){assert(!esp32qjs_eap_task_started);os_free(p);}
static int wpa2_start_eapol_internal(void){assert(in_worker && gEapSm && !config_frees);return 0;}
static int eap_sm_rx_eapol_internal(u8 *a,u8 *b,u32 c,u8 *d){(void)a;(void)b;(void)c;(void)d;assert(in_worker);return 0;}
static int eloop_cancel_timeout(void (*fn)(void *,void *),void *a,void *b){assert(fn==eap_start_eapol && !a && !b && !in_worker);cancelled++;return 0;}
static int esp_wifi_unregister_wpa2_cb_internal(void){assert(!gEapSm && !esp32qjs_eap_task_started);return unregister_error;}
static int esp_eap_client_get_disable_time_check(bool *disabled){*disabled=false;return ESP_OK;}
static void eap_globals_reset(void){assert(!gEapSm && !esp32qjs_eap_task_started);global_resets++;}
static void eap_peer_unregister_methods(void){method_unregisters++;}
bool current_task_is_wifi_task(void){return false;}
static int eloop_register_timeout_blocking(int (*fn)(void *,void *),void *a,void *b){return dispatch_fail?-1:fn(a,b);}
