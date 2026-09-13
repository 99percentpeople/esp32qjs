
#define WIFI_IF_NAN 2
#define NAN_ND_GTK_LEN 16
#define NAN_WIFI_WPA_ALG_BIP_CMAC_128 6
#define NAN_KEY_ND_IGTK 3
#define NAN_KEY_ND_BIGTK 4
static struct{bool group_mgmt_prot,own_igtk_set,own_bigtk_set;uint8_t own_igtk_keyid,own_bigtk_keyid;
 uint8_t own_igtk[16],own_bigtk[16],own_igtk_ipn[6],own_bigtk_bipn[6];}s_nan_ctx;
static int mac_error,ensure_error,install_error_at,installs,ensures;
static int esp_wifi_get_mac(int iface,uint8_t*out){(void)iface;memset(out,2,6);return mac_error;}
static int nan_ensure_own_igtk(void){++ensures;s_nan_ctx.own_igtk_set=!ensure_error;return ensure_error;}
static int nan_ensure_own_bigtk(void){++ensures;s_nan_ctx.own_bigtk_set=!ensure_error;return ensure_error;}
static int esp_wifi_set_nan_key_internal(int alg,const uint8_t*mac,int id,int tx,const uint8_t*rsc,int rsc_len,
 const uint8_t*key,int length,int type){
 (void)alg;(void)mac;(void)id;(void)tx;(void)rsc;(void)rsc_len;(void)key;(void)length;(void)type;
 return ++installs==install_error_at?-73:0;
}
