
static uint32_t esp32qjs_wps_ap_result_identity(const void *p){return p!=NULL;}
static bool esp32qjs_wps_ap_result_deinit_allowed(void *p){return p!=NULL;}
static void esp32qjs_wps_ap_result_cleanup_error(void *p,int e){(void)p;(void)e;}
static int esp32qjs_wps_ap_hostap_release(void *p){return p?ESP_OK:ESP_ERR_INVALID_STATE;}
static int esp32qjs_wps_ap_result_bind(void *p){(void)p;return ESP_OK;}
static void esp32qjs_wps_ap_result_detach(void *p,int err){(void)p;(void)err;}
static int esp32qjs_wps_ap_result_event(void *p,int event,const void *data,size_t n){(void)p;(void)event;(void)data;(void)n;return ESP_OK;}
#define WSC_ID_REGISTRAR_LEN 3
#define WSC_ID_REGISTRAR "reg"
#define WIFI_IF_AP 1
#define WPS_TYPE_PIN 2
#define WIFI_EVENT 1
#define WIFI_EVENT_AP_WPS_RG_PIN 2
#define OS_BLOCK 0
struct wps_sm {u8 ownaddr[6],identity[32],uuid[16];size_t identity_len;struct wps_context *wps_ctx;struct wps_device_data *dev;struct wps_data *wps;};
struct wps_config {int registrar;struct wps_context *wps;u8 pin[9];};
typedef struct {u8 pin[9];} esp_wps_config_t;
typedef struct {u8 pin_code[8];} wifi_event_sta_wps_er_pin_t;
static struct wps_sm *gWpsSm;static struct hostapd_data h;
static int injected_host_error;static bool data_freed,context_freed;
static struct hostapd_data *hostapd_get_hapd_data(void){return &h;}
static int esp_wifi_get_macaddr_internal(int iface,u8 mac[6]){(void)iface;memset(mac,2,6);return 0;}
static int wps_dev_init(void){gWpsSm->dev=&gWpsSm->wps_ctx->dev;return 0;}
static int wps_init_cfg_pin(struct wps_config *cfg){(void)cfg;return 0;}
static struct wps_data *wps_init(struct wps_config *cfg){struct wps_data *d=calloc(1,sizeof(*d));d->wps=cfg->wps;return d;}
static int hostapd_init_wps(struct hostapd_data *hapd,struct wps_data *data,struct wps_context *ctx){(void)hapd;(void)data;(void)ctx;return injected_host_error;}
static int wps_get_type(void){return 1;}
static void esp_event_post(int base,int id,const void *p,size_t n,int wait){(void)base;(void)id;(void)p;(void)n;(void)wait;}
static void wps_deinit(struct wps_data *data){assert(!context_freed && data->wps==gWpsSm->wps_ctx);data_freed=true;free(data);}
static int wps_dev_deinit(struct wps_device_data *dev){assert(data_freed && !context_freed && dev==&gWpsSm->wps_ctx->dev);return 0;}
static void native_clear_free(void *p,size_t n){if(gWpsSm && p==gWpsSm->wps_ctx){assert(data_freed);context_freed=true;}bin_clear_free(p,n);}
#define bin_clear_free native_clear_free
