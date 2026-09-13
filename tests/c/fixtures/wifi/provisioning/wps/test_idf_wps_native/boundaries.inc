
static struct wps_sm {struct wps_credential creds[MAX_CRED_COUNT];u8 ap_cred_cnt;void *wps;int state;u8 bssid[6];} sample,*gWpsSm;
static bool on_native=true,fail_alloc,foreign_owner,dpp_enabled,success_pending;
static int enable_error,start_error,disconnect_error,type_error,disable_error,timer_error;
static unsigned allocations,enable_calls,start_calls,disconnect_calls,type_calls,disable_calls,posts;
static void *allocation,*timer_high,*timer_low;
static size_t allocation_size;
static eloop_timeout_handler timer_handler;
static void wifi_station_wps_success(void *a,void *b) {(void)a;(void)b;}
static void wifi_station_wps_timeout(void *a,void *b);
static void wifi_station_wps_msg_timeout(void *a,void *b) {(void)a;(void)b;}
static void wifi_wps_scan(void *a,void *b) {(void)a;(void)b;}
static void wifi_station_wps_eapol_start_handle(void *a,void *b) {(void)a;(void)b;}
static void wifi_station_wps_post_m8_timeout(void *a,void *b) {(void)a;(void)b;}
static struct {void *wpa_sm_wps_disable;} gWpaSm;
static wps_factory_information_t *s_factory_info;
static unsigned char s_wps_success_evt[64];
static unsigned char scan_storage[72];
void *g_scan=scan_storage;
static void *s_wps_sm_cb;
static int wps_get_type(void) {return WPS_TYPE_DISABLE;}
static int native_wps_status,associated_error;
static uint8_t associated_bssid[6];
static int wps_get_status(void) {return native_wps_status;}
static int esp_wifi_get_assoc_bssid_internal(uint8_t *out) {
    memcpy(out,associated_bssid,6);return associated_error;
}
static void bin_clear_free(void *p,size_t n) {
    assert(p==allocation && n==allocation_size && allocations==1);
    memset(p,0,n);free(p);allocation=NULL;allocations=0;
}
static unsigned cleanup_calls[ESP32QJS_WPS_CLEANUP_DONE],deinit_calls;
static int cleanup_fail_stage=-1,deinit_error;
static unsigned cancel_count,fail_cancel_at=UINT32_MAX;
static int cleanup_step(unsigned stage) {
    cleanup_calls[stage]++;
    return (int)stage==cleanup_fail_stage ? 300+(int)stage : ESP_OK;
}
static int esp_wifi_set_wps_start_flag_internal(bool flag) {
    assert(!flag);return cleanup_step(ESP32QJS_WPS_CLEANUP_START_FLAG);
}
static int esp_wifi_scan_stop(void) {return cleanup_step(ESP32QJS_WPS_CLEANUP_SCAN);}
static int eloop_cancel_timeout(eloop_timeout_handler f,void *a,void *b) {
    assert(f && a==ELOOP_ALL_CTX && b==ELOOP_ALL_CTX);
    int error=cleanup_step(ESP32QJS_WPS_CLEANUP_TIMERS);
    if(error)return -error;
    if(cancel_count==fail_cancel_at)return -301;
    cancel_count++;return 0;
}
static int esp_wifi_disarm_sta_connection_timer_internal(void) {
    return cleanup_step(ESP32QJS_WPS_CLEANUP_CONNECTION_TIMER);
}
static int esp_wifi_set_wps_cb_internal(void *cb) {
    assert(!cb);disable_calls++;
    int error=cleanup_step(ESP32QJS_WPS_CLEANUP_CALLBACKS);
    return error?error:disable_error;
}
static int esp_wifi_unset_appie_internal(int ie) {
    return cleanup_step(ie==WIFI_APPIE_WPS_PR ? ESP32QJS_WPS_CLEANUP_PROBE_IE : ESP32QJS_WPS_CLEANUP_ASSOC_IE);
}
static int wps_set_status(int status) {
    assert(status==WPS_STATUS_DISABLE);int error=cleanup_step(ESP32QJS_WPS_CLEANUP_STATUS);
    if(!error)native_wps_status=status;return error;
}
static void wps_set_owner(int owner) {assert(owner==WPS_OWNER_NONE);}
static void os_free(void *p) {free(p);}
static int wifi_station_wps_deinit(void) {
    deinit_calls++;if(!deinit_error)gWpsSm=NULL;return deinit_error;
}
static struct wps_sm *wps_sm_get(void) {return gWpsSm;}
bool current_task_is_wifi_task(void) {return on_native;}
static int wps_get_owner(void) {return foreign_owner?2:0;}
static bool is_dpp_enabled(void) {return dpp_enabled;}
static int esp_wifi_get_user_init_flag_internal(void) {return 1;}
static int wps_check_wifi_mode(void) {return ESP_OK;}
static void *os_zalloc(size_t n) {
    if(fail_alloc)return NULL;assert(!allocation);
    allocation=calloc(1,n);assert(allocation);allocations++;allocation_size=n;return allocation;
}
static int eloop_is_timeout_registered(eloop_timeout_handler f,void *a,void *b) {
    assert(f==wifi_station_wps_success && !a && !b);return success_pending;
}
static int eloop_register_timeout(unsigned s,unsigned us,eloop_timeout_handler f,void *a,void *b) {
    (void)s;(void)us;timer_handler=f;timer_high=a;timer_low=b;return timer_error;
}
static int esp_wifi_disconnect(void) {
    disconnect_calls++;int error=cleanup_step(ESP32QJS_WPS_CLEANUP_DISCONNECT);
    return error?error:disconnect_error;
}
static int wps_set_type(uint32_t type) {
    assert(type==WPS_TYPE_DISABLE);type_calls++;int error=cleanup_step(ESP32QJS_WPS_CLEANUP_TYPE);
    return error?error:type_error;
}
static int wifi_wps_disable_internal(void *ctx,void *data) {
    (void)ctx;(void)data;disable_calls++;if(!disable_error)gWpsSm=NULL;return disable_error;
}
static int esp_event_post(int base,int id,const void *p,size_t n,uint32_t wait) {
    (void)base;(void)id;(void)p;(void)n;(void)wait;posts++;return ESP_ERR_TIMEOUT;
}
static int wifi_wps_enable_internal(void *ctx,void *data);
static int wifi_station_wps_start(void *data,void *user);
