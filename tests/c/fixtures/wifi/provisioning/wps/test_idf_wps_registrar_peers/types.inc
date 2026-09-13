
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
typedef uint8_t u8;
#define ESP_SUPPLICANT 1
#define ESP_OK 0
#define ESP_ERR_INVALID_STATE 0x103
#define ESP_ERR_NOT_FINISHED 0x10c
#define WSC_ID_ENROLLEE "WFA-SimpleConfig-Enrollee-1-0"
#define WSC_ID_ENROLLEE_LEN (sizeof(WSC_ID_ENROLLEE)-1)
#define WSC_ID_REGISTRAR "WFA-SimpleConfig-Registrar-1-0"
#define WSC_ID_REGISTRAR_LEN (sizeof(WSC_ID_REGISTRAR)-1)
#define WPS_OWNER_REGISTRAR 2
#define WPS_OWNER_ENROLLEE 1
#define WPS_STATUS_PENDING 2
#define WSC_FRAGMENT_SIZE 1400
#define ETH_ALEN 6
#define MSG_INFO 0
#define os_memcmp memcmp
#define os_memcpy memcpy
#define wpa_hexdump_ascii(...) ((void)0)
static int allocations,fail_at,live,disable_calls,protocol_frees,buffer_clears;
static void *os_zalloc(size_t n){if(++allocations==fail_at)return NULL;void *p=calloc(1,n);assert(p);live++;return p;}
static void os_free(void *p){if(p){live--;free(p);}}
static void forced_memzero(void *p,size_t n){volatile u8 *v=p;while(n--)*v++=0;}
static void bin_clear_free(void *p,size_t n){if(p){forced_memzero(p,n);for(size_t i=0;i<n;i++)assert(!((u8*)p)[i]);os_free(p);}}
static void *os_memdup(const void *p,size_t n){void *r=os_zalloc(n);if(r)memcpy(r,p,n);return r;}
struct wpabuf{u8 bytes[16];};
static void wpabuf_free(struct wpabuf *p){os_free(p);}
static void wpabuf_clear_free(struct wpabuf *p){if(p){buffer_clears++;bin_clear_free(p,sizeof(*p));}}
struct wps_credential{u8 ssid[32],key[64];};
struct wps_context{void *registrar;};
struct wps_data{struct wps_context *wps;struct wps_credential *use_cred;int pbc,dev_pw_id,state;struct {u8 mac_addr[6];} peer_dev;u8 mac_addr_e[6];};
struct wps_sm{struct wps_data *wps;struct wps_context *wps_ctx;};
struct wps_config{struct wps_context *wps;int registrar,pbc,dev_pw_id;};
struct eap_config{struct wps_context *wps;};
struct eapol_authenticator{struct {const struct eap_config *eap_cfg;} conf;};
struct hostapd_data{struct wps_context *wps;struct eapol_authenticator *eapol_auth;};
struct eap_sm{u8 *identity;size_t identity_len;const struct eap_config *cfg;u8 peer_addr[6];void *eap_method_priv;};
struct eap_wsc_data{enum{START,MESG,FRAG_ACK,WAIT_FRAG_ACK,DONE,WSC_FAIL}state;int registrar;struct wpabuf *in_buf,*out_buf;struct wps_data *wps;size_t fragment_size;};
static struct wps_context context;
static struct wps_credential credential;
static struct wps_data template;
static struct wps_sm owner;
static struct hostapd_data hapd;
static struct eap_config cfg;
static struct eap_sm peers[2];
static int owner_kind,owner_status;
static struct wps_sm *wps_sm_get(void){return &owner;}
static struct hostapd_data *hostapd_get_hapd_data(void){return &hapd;}
static int wps_get_owner(void){return owner_kind;}
static int wps_get_status(void){return owner_status;}
static int wifi_ap_wps_disable_internal(void){disable_calls++;return 0;}
static struct wps_data *wps_init(const struct wps_config *c){assert(c->registrar==1 && c->wps==&context);struct wps_data *p=os_zalloc(sizeof(*p));if(p){p->wps=c->wps;p->pbc=c->pbc;}return p;}
static void wps_deinit(struct wps_data *p){assert(p!=&template && p->wps->registrar);protocol_frees++;bin_clear_free(p->use_cred,sizeof(*p->use_cred));bin_clear_free(p,sizeof(*p));}
static void setup(void){
 allocations=fail_at=live=disable_calls=protocol_frees=buffer_clears=0;
 context=(struct wps_context){.registrar=(void*)(uintptr_t)1};
 memset(&credential,0x55,sizeof(credential));
 template=(struct wps_data){.wps=&context,.use_cred=&credential,.pbc=1};
 owner=(struct wps_sm){.wps=&template,.wps_ctx=&context};
 cfg=(struct eap_config){.wps=&context};hapd=(struct hostapd_data){.wps=&context};
 for(int i=0;i<2;i++)peers[i]=(struct eap_sm){.identity=(u8*)WSC_ID_ENROLLEE,.identity_len=WSC_ID_ENROLLEE_LEN,.cfg=&cfg,.peer_addr={2,0,0,0,0,i+1}};
 owner_kind=WPS_OWNER_REGISTRAR;owner_status=WPS_STATUS_PENDING;
}
