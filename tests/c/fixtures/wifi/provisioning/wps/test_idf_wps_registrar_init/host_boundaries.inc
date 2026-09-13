
static uint32_t esp32qjs_wps_host_methods_identity;
static int esp32qjs_wps_host_ie_error;
#define EAP_VENDOR_IETF 0
#define EAP_TYPE_IDENTITY 1
#define EAP_VENDOR_WFA 2
#define EAP_VENDOR_TYPE_WSC 3
static int fail_step,step,borrowed_frees,registered,registrars,ie_clears;
static uint32_t esp32qjs_wps_ap_result_identity(const void *hapd){(void)hapd;return 1;}
static int hostapd_wps_config_ap(struct hostapd_data *hapd,struct wps_data *data){(void)hapd;(void)data;return ++step==fail_step?ESP_ERR_NO_MEM:ESP_OK;}
static void hostapd_wps_event_cb(void){}
static int hostapd_wps_rf_band_cb(void){return 1;}
static int esp32qjs_wps_ap_rf_band_read(int *band){*band=1;return ESP_OK;}
static int hostapd_wps_set_ie_cb(void){return 0;}
static void hostapd_wps_reg_success_cb(void){}
static void *eap_server_get_eap_method(int vendor,int method){(void)vendor;(void)method;return NULL;}
static void *wps_registrar_init(struct wps_context *ctx,struct wps_registrar_config *cfg){(void)ctx;(void)cfg;if(++step==fail_step)return NULL;registrars++;return malloc(1);}
static void wps_registrar_deinit(void *p){if(p){assert(registrars);registrars--;free(p);}}
static int eap_server_identity_register(void){if(++step==fail_step)return -1;registered++;return 0;}
static int eap_server_wsc_register(void){if(++step==fail_step)return -1;registered++;return 0;}
static void eap_server_unregister_methods(void){registered=0;}
static int ieee802_1x_init(struct hostapd_data *hapd){(void)hapd;return ++step==fail_step?-1:0;}
static void hostapd_wps_clear_ies(struct hostapd_data *hapd,int only){(void)hapd;(void)only;ie_clears++;}
static void hostapd_free_wps(struct wps_context *ctx){(void)ctx;borrowed_frees++;}
