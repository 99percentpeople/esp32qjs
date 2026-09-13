
static esp32qjs_wps_native_t record,*s_wps_native=&record;
static struct wps_sm *gWpsSm;
static struct wps_sm_funcs *s_wps_sm_cb;
static struct {uint8_t own_addr[6];} gWpaSm;
static uint8_t s_wps_success_evt[32];
static struct wps_data protocol;
static unsigned nth,fail_nth,live,config_calls,connect_calls,timers,tx_frees;
static int mac_error,device_error,pin_error,protocol_null,callback_error,status_error,config_error,connect_error;
static int current_status,associated_error,tx_error,parse_error;
static bool tx_oom;
static void *adopted;
static uint8_t tx_storage[128];static size_t tx_length;
static void *os_zalloc(size_t size) {nth++;if(nth==fail_nth)return NULL;void *p=calloc(1,size);assert(p);live++;return p;}
static void os_free(void *p) {if(p){assert(live);live--;free(p);}}
static void bin_clear_free(void *p,size_t size) {if(p){memset(p,0,size);os_free(p);}}
static struct wps_sm *wps_sm_get(void) {return gWpsSm;}
static int wps_get_type(void) {return WPS_TYPE_PIN;}
static int wps_get_status(void) {return current_status;}
static int wps_set_status(int status) {if(!status_error)current_status=status;return status_error;}
static struct wpabuf *wps_build_probe_req_ie(int id,void *dev,void *uuid,int req,int pbc,void *extra) {
    (void)id;(void)dev;(void)uuid;(void)req;(void)pbc;(void)extra;return probe_oom?NULL:&probe;
}
static struct wpabuf *wps_build_assoc_req_ie(int req) {(void)req;return assoc_oom?NULL:&assoc;}
static int wpabuf_resize(struct wpabuf **b,size_t size) {(void)size;if(!resize_error)*b=&extra;return resize_error;}
static void wpabuf_put_buf(struct wpabuf *b,const struct wpabuf *v) {(void)b;(void)v;}
static int esp_wifi_set_appie_internal(int type,uint8_t *data,size_t len,int flag) {
    (void)type;(void)data;(void)len;(void)flag;return appie_error;
}
static void *esp_wifi_get_appie_internal(int type) {(void)type;return NULL;}
static int esp_wifi_unset_appie_internal(int type) {(void)type;return ESP_OK;}
static int esp_wifi_get_macaddr_internal(int itf,uint8_t *out) {(void)itf;memset(out,1,6);return mac_error;}
static int wps_dev_init(void) {gWpsSm->dev=&gWpsSm->wps_ctx->dev;return device_error;}
static int wps_dev_deinit(void *dev) {(void)dev;return 0;}
static int wps_init_cfg_pin(struct wps_config *cfg) {(void)cfg;return pin_error;}
static struct wps_data *wps_init(struct wps_config *cfg) {
    protocol.wps=cfg->wps;memcpy(protocol.dev_password,"12345670",8);return protocol_null?NULL:&protocol;
}
static void wps_deinit(struct wps_data *p) {(void)p;}
static int save_credentials_cb(void) {return 0;}
static int wps_delete_timer(void) {return 0;}
static int wps_parse_scan_result(void) {return 0;}
static int wps_sm_rx_eapol(void) {return 0;}
static int wps_start_pending(void) {return 0;}
static int esp_wifi_set_wps_cb_internal(void *p) {
    if(p && callback_error)return callback_error;os_free(adopted);adopted=p;return 0;
}
static int esp_event_post(int base,int event,void *p,size_t len,int wait) {
    (void)base;(void)event;(void)p;(void)len;(void)wait;assert(0);return 0;
}
static int esp_wifi_set_config(int itf,const wifi_config_t *cfg) {
    assert(itf==WIFI_IF_STA && cfg->sta.failure_retry_cnt==2);config_calls++;return config_error;
}
static int esp_wifi_connect(void) {connect_calls++;return connect_error;}
static void wifi_station_wps_msg_timeout(void *a,void *b) {(void)a;(void)b;}
static void wifi_wps_scan(void *a,void *b) {(void)a;(void)b;}
static int eloop_cancel_timeout(void (*f)(void*,void*),void *a,void *b) {(void)f;(void)a;(void)b;return 0;}
static int esp32qjs_wps_register_timeout(unsigned s,unsigned us,void (*f)(void*,void*)) {(void)s;(void)us;(void)f;timers++;return 0;}
static int wps_send_event_and_disable(int event,void *p,size_t size) {(void)event;(void)p;(void)size;return 0;}
static int wpa_parse_wpa_ie_rsn(const uint8_t *ie,size_t len,struct wpa_ie_data *info) {
    (void)ie;(void)len;info->key_mgmt=WPA_KEY_MGMT_SAE;return parse_error;
}
static int esp_wifi_get_assoc_bssid_internal(uint8_t *out) {memset(out,2,6);return associated_error;}
static int wpa_ether_send(void *sm,const uint8_t *bssid,uint16_t proto,const uint8_t *data,size_t len) {
    (void)sm;(void)bssid;(void)proto;assert(data==tx_storage && len==tx_length);return tx_error;
}
static uint8_t *wps_sm_alloc_eapol(struct wps_sm *sm,uint8_t type,const void *data,uint16_t size,size_t *len,void **pos) {
    (void)sm;(void)type;(void)pos;if(tx_oom)return NULL;assert(size+4<=sizeof(tx_storage));
    memset(tx_storage,0xaa,sizeof(tx_storage));memcpy(tx_storage+4,data,size);*len=tx_length=size+4;return tx_storage;
}
static void wps_sm_free_eapol(uint8_t *buf) {
    assert(buf==tx_storage);for(size_t i=0;i<tx_length;i++)assert(!buf[i]);tx_frees++;
}
