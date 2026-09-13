
static bool on_wifi,on_eap,api_locked,fail_alloc,fail_lock,fail_dispatch,fail_methods,fail_retire,adopt_on_error;
static int register_error,unregister_error,driver_error;
static bool driver_enabled;
static struct wpa2_funcs *driver_callbacks;
static unsigned live,allocations,dispatches,enables,disables,registers,unregisters,methods,method_clears,global_clears;
static unsigned sae_frees,wps_frees,wpa_frees,roam_frees;
static int eap_task_marker;
bool current_task_is_wifi_task(void){return on_wifi;}
static void *os_task_get_current_task(void){return on_eap?&eap_task_marker:NULL;}
static void *os_zalloc(size_t n){assert(on_wifi);allocations++;if(fail_alloc){fail_alloc=false;return NULL;}void *p=calloc(1,n);assert(p);live++;return p;}
static void os_free(void *p){if(p){live--;free(p);}}
static void *os_recursive_mutex_create(void){assert(on_wifi);return os_zalloc(1);}
static int os_mutex_lock(void *p){assert(on_wifi && p && !api_locked);if(fail_lock)return 0;api_locked=true;return TRUE;}
static void os_mutex_unlock(void *p){assert(on_wifi && p && api_locked);api_locked=false;}
static int eloop_register_timeout_blocking(int (*fn)(void *,void *),void *a,void *b){
    assert(!on_wifi && !api_locked);dispatches++;if(fail_dispatch)return -1;
    on_wifi=true;int result=fn(a,b);on_wifi=false;assert(!api_locked);return result;
}
static int esp_wifi_sta_wpa2_ent_enable_internal(wifi_wpa2_param_t *p){assert(on_wifi && api_locked);enables++;if(driver_error)return driver_error;driver_enabled=true;return p->fn(p->param);}
static int esp_wifi_sta_wpa2_ent_disable_internal(wifi_wpa2_param_t *p){assert(on_wifi && api_locked);disables++;if(driver_error)return driver_error;driver_enabled=false;return p->fn(p->param);}
static int esp_wifi_register_wpa2_cb_internal(struct wpa2_funcs *p){assert(on_wifi && api_locked);registers++;if(driver_callbacks)os_free(driver_callbacks);driver_callbacks=NULL;if(!register_error || adopt_on_error)driver_callbacks=p;return register_error;}
static int esp_wifi_unregister_wpa2_cb_internal(void){assert(on_wifi && api_locked);unregisters++;if(unregister_error)return unregister_error;os_free(driver_callbacks);driver_callbacks=NULL;return 0;}
static int eap_peer_register_methods(void){assert(on_wifi && api_locked);methods=1;return fail_methods?-1:0;}
static void eap_peer_unregister_methods(void){assert(on_wifi && api_locked);methods=0;method_clears++;}
static int wpa2_ent_rx_eapol(u8 *a,u8 *b,u32 n,u8 *c){(void)a;(void)b;(void)n;(void)c;return 0;}
static int eap_start_eapol_timer(void){return 0;}
static int eap_peer_sm_init(void){return 0;}
static void eap_peer_sm_deinit(void){assert(on_wifi && api_locked);if(fail_retire){esp32qjs_eap_retiring=true;esp32qjs_eap_cleanup_error=77;return;}gEapSm=NULL;esp32qjs_eap_retiring=false;esp32qjs_eap_cleanup_error=0;}
static void eap_globals_reset(void){assert(on_wifi && api_locked && !gEapSm && !driver_callbacks);global_clears++;g_wpa_password=NULL;}
void esp_wifi_set_okc_support(bool value){assert(on_wifi && api_locked && value);}
static void esp_wpa3_free_sae_data(void){sae_frees++;}
static void roam_deinit_app(void){roam_frees++;}
static int clear_wps(void){wps_frees++;return 0;}
static int esp_wifi_register_eapol_txdonecb_internal(void *p){assert(!p);return 0;}
static void wpa_sm_deinit(void){wpa_frees++;}
