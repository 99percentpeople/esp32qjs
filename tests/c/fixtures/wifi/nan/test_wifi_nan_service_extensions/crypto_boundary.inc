
#define ESP_LOGE(...) ((void)0)
#define ESP_LOGD(...) ((void)0)
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
#define ESP_LOG_BUFFER_HEXDUMP(...) ((void)0)
#define NAN_PMK_NAME_LABEL "NAN PMK Name"
#define NAN_PMK_NAME_LABEL_LEN 12
#define ESP_WIFI_NAN_NDP_PMKID_LEN 16
#define WIFI_NAN_SECURITY_ENCRYPTED 1
#define NAN_CSID_GTK_DEFAULT 32
#define WIFI_IF_NAN 2
typedef struct {int type;uint16_t csid_bitmap;bool group_data_prot,group_mgmt_prot;uint8_t nd_pmk[32],nd_pmkid[16];}wifi_nan_security_params_t;
struct own_svc_info{wifi_nan_discovery_security_params_t user_cfg;wifi_nan_security_params_t derived_security[4];};
static struct own_svc_info own;
static unsigned hmac_calls;static int failure_mode;
static void forced_memzero(void*p,size_t n){volatile uint8_t*b=p;while(n--)*b++=0;}
static bool nan_compute_service_id(const char*n,uint8_t*out){(void)n;memset(out,1,6);return true;}
static struct own_svc_info*nan_find_own_svc_by_name(const char*n){(void)n;return &own;}
static int esp_wifi_get_mac(int iface,uint8_t*out){(void)iface;memset(out,2,6);return failure_mode==1?-77:0;}
static int nan_derive_nd_pmk_from_passphrase(const char*p,uint8_t c,const uint8_t*s,const uint8_t*m,uint8_t*out){
 (void)p;(void)c;(void)s;(void)m;memset(out,3,32);return 0;
}
static int hmac(const unsigned char*k,int n,int count,const unsigned char**a,int*l,unsigned char*out){
 (void)k;(void)n;(void)count;(void)a;(void)l;memset(out,0x5a,32);
 return ++hmac_calls==2&&failure_mode==2?-88:0;
}
static struct{int(*hmac_sha256_vector)(const unsigned char*,int,int,const unsigned char**,int*,unsigned char*);}g_wifi_default_wpa_crypto_funcs={hmac};
