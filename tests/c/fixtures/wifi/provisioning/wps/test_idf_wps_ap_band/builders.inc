
typedef uint16_t u16;
typedef uint8_t u8;
struct wpabuf {int unused;};
struct wps_device_data {int rf_bands;};
struct wps_context {int (*rf_band_cb)(void *);void *cb_ctx;struct wps_device_data dev;int ap,ap_setup_locked,config_methods;};
struct wps_data {struct wps_context *wps;int config_error,int_reg,state,pbc_in_m1,dev_password_len,multi_ap_backhaul_sta;};
enum {RECV_DONE,RECV_M3,RECV_M2D_ACK,RECV_M2,WPS_CFG_NO_ERROR,WPS_CFG_SETUP_LOCKED};
enum {WPS_CONFIG_DISPLAY=1,WPS_CONFIG_PUSHBUTTON=2,WPS_CONFIG_VIRT_PUSHBUTTON=4,WPS_CONFIG_PHY_PUSHBUTTON=8,MULTI_AP_BACKHAUL_STA=1};
static struct wpabuf message;
static int rf_result,callbacks,allocations,crypto,encoded;
static bool allocation_failure;
static int callback(void *ctx){(void)ctx;++callbacks;return rf_result;}
static struct wpabuf *wpabuf_alloc(size_t bytes){assert(bytes==1000);++allocations;return allocation_failure?NULL:&message;}
static void wpabuf_free(struct wpabuf *msg){assert(msg==&message);}
static int encode(int band){encoded=band;return 0;}
static int random_bytes(void){++crypto;return 0;}
#define random_get_bytes(...) random_bytes()
#define wpa_hexdump(...) ((void)0)
#define wps_build_version(...) 0
#define wps_build_msg_type(...) 0
#define wps_build_enrollee_nonce(...) 0
#define wps_build_registrar_nonce(...) 0
#define wps_build_uuid_r(...) 0
#define wps_build_uuid_e(...) 0
#define wps_build_mac_addr(...) 0
#define wps_build_config_methods(...) 0
#define wps_build_wps_state(...) 0
#define wps_build_vendor_ext_m1(...) 0
#define wps_build_public_key(...) 0
#define wps_derive_keys(...) 0
#define wps_build_auth_type_flags(...) 0
#define wps_build_encr_type_flags(...) 0
#define wps_build_conn_type_flags(...) 0
#define wps_build_config_methods_r(...) 0
#define wps_build_device_attrs(...) 0
#define wps_build_rf_bands(dev,msg,band) encode(band)
#define wps_build_assoc_state(...) 0
#define wps_build_config_error(...) 0
#define wps_build_dev_password_id(...) 0
#define wps_build_os_version(...) 0
#define wps_build_wfa_ext(...) 0
#define wps_build_authenticator(...) 0
