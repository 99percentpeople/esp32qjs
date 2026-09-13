
static int depth,live,fail_alloc,unregister_error,post_error,quiescent_error,drain_error;
static unsigned unregister_fence,heap_frees,post_count,cancels;
static bool tx_held,tx_cancelled;
static bool wifi_task=true,wps_held;
static bool s_dpp_init_pending,s_dpp_deinit_dispatch_pending;
static unsigned s_dpp_async_count,s_dpp_async_active;
static atomic_bool s_dpp_init_done,dpp_shutting_down,roc_in_progress;
static struct {bool dpp_deinit_pending,dpp_listen_ongoing;void*dpp_global,*dpp_config_store;}s_dpp_ctx;
static void *s_action_rx_cb=(void*)0x1234;
static int dpp_api_lock(void){depth++;return 0;}
static int dpp_api_unlock(void){assert(depth);depth--;return 0;}
static bool current_task_is_wifi_task(void){return wifi_task;}
static bool esp32qjs_wps_native_held(void){return wps_held;}
static int wps_get_owner(void){return 0;}
static void *os_zalloc(size_t n){if(fail_alloc){fail_alloc=0;return NULL;}void*p=calloc(1,n);assert(p);live++;return p;}
static void bin_clear_free(void*p,size_t n){if(p){volatile unsigned char*q=p;for(size_t i=0;i<n;i++)q[i]=0;live--;free(p);}}
static bool dpp_akm_dpp(enum dpp_akm x){return x==1||x==5||x==6;}
/* This fixture covers result/SDK teardown; ROC's real producer/retirement
 * implementation is exercised separately by test_dpp_roc_control.py. */
static bool esp32qjs_dpp_roc_held(void){return false;}
static void esp32qjs_dpp_roc_status_locked(esp32_mquickjs_wifi_dpp_result_status_t*out){(void)out;}
static int esp32qjs_dpp_roc_cancel_locked(void){return 0;}
static bool esp32qjs_dpp_tx_held(void){return tx_held;}
static void esp32qjs_dpp_tx_status_locked(esp32_mquickjs_wifi_dpp_result_status_t*out){(void)out;}
static void esp32qjs_wifi_chm_timer_status(esp32qjs_wifi_chm_timer_status_t*out){
 *out=(esp32qjs_wifi_chm_timer_status_t){.post_failures=2,.timers_held=2};
}
static int esp32qjs_dpp_tx_cancel_locked(void){
 if(!tx_held)return 0;
 if(!tx_cancelled){tx_cancelled=true;cancels++;}
 if(quiescent_error)return quiescent_error;
 tx_held=false;return 0;
}
static int esp32qjs_dpp_bootstrap_cancel_locked(void){return 0;}
static void esp_dpp_cancel_timeouts(void){}
static int esp32qjs_dpp_async_drain_locked(void){return drain_error;}
static void dpp_stop_internal(void){}
static int esp32_mquickjs_wifi_action_sdk_quiescent(void){return quiescent_error;}
static int esp_event_handler_register(int base,int id,void(*fn)(void*,int,int,void*),void*arg){(void)base;(void)id;(void)fn;(void)arg;assert(!depth);return 0;}
static int esp_event_handler_unregister(int base,int id,void(*fn)(void*,int,int,void*)){
 (void)fn;assert(!depth&&base==100&&!id);unregister_fence++;return unregister_error;
}
static int esp_event_post(int base,int id,const void*data,size_t size,int wait){assert(base==100&&!id&&data&&size==8&&!wait&&depth);post_count++;return post_error;}
static void dpp_global_deinit(void*p){assert(p);heap_frees++;}
static void dpp_config_store_deinit(void*p){assert(p);heap_frees++;}
static void esp_wifi_sta_notify_dpp_config_set_internal(bool set){assert(!set);}
