
#include <stdlib.h>
#include <string.h>
#define CONFIG_LWIP_ND6_SUPPORT_STATIC_ENTRIES 1
#define NDP_STATUS_ACCEPTED 1
#define NDP_STATUS_REJECTED 2
#define NDP_ACCEPTED 1U
#define NDP_REJECTED 2U
#define WIFI_IF_NAN 3
#define WIFI_EVENT_NDP_CONFIRM 4
#define WIFI_EVENT 5
#define NAN_IPV6_IDENTIFIER_LEN 8
#define MACADDR_COPY(a,b) memcpy(a,b,6)
#define MACADDR_EQUAL(a,b) (!memcmp(a,b,6))
#define MACSTR "peer"
#define MAC2STR(p) 0
#define ESP_LOGE(...) ((void)0)
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
#define ESP_LOGD(...) ((void)0)
#define ESP_LOG_BUFFER_HEXDUMP(...) ((void)0)
#define PP_HTONL(v) (v)
#define IS_ZERO_NAN_IPV6_IDENTIFIER(p) (!memcmp(p,"\0\0\0\0\0\0\0\0",8))
typedef unsigned EventBits_t;
typedef void *wifi_netif_driver_t;
typedef struct {uint32_t addr[4];} ip6_addr_t;
typedef struct {struct {ip6_addr_t ip6;} u_addr;} ip_addr_t;
typedef struct {uint8_t ndp_id,peer_mac[6];} wifi_nan_datapath_end_req_t;
typedef struct {uint8_t status,ndp_id,peer_nmi[6],peer_ndi[6],own_ndi[6],ipv6_identifier[8];uint16_t ssi_len;uint8_t ssi[];} wifi_event_ndp_confirm_t;
struct ndl_info {uint8_t peer_ndi[6];};
struct ndp_cb_peer_info {uint8_t ndp_id,peer_nmi[6],peer_ndi[6];uint8_t*ssi;uint16_t ssi_len;};
static struct {void*nan_netif;} s_nan_ctx={(void*)1};
static unsigned depth,resets,end_calls,notices,accepted,posts;
static int rx_error,end_error,failed_error,cleanup_error;
static bool live_ndl=true,fail_event;
static unsigned event_bits;
static void*nan_event_group=(void*)2;
static uint8_t peer[6]={2,3,4,5,6,7},null_mac[6];
static struct ndl_info ndl;
#define NAN_DATA_LOCK() do{assert(!depth);++depth;}while(0)
#define NAN_DATA_UNLOCK() do{assert(depth==1);--depth;}while(0)
static struct ndl_info*nan_find_ndl(uint8_t id,uint8_t*mac){assert(depth==1);return live_ndl&&id==7&&MACADDR_EQUAL(mac,peer)?&ndl:NULL;}
static void nan_reset_ndl(uint8_t id,bool all){assert(depth==1&&id==7&&!all);++resets;live_ndl=false;}
static esp_err_t esp_nan_internal_datapath_end(wifi_nan_datapath_end_req_t*req){assert(!depth&&req->ndp_id==7&&MACADDR_EQUAL(req->peer_mac,peer));++end_calls;return end_error;}
static void os_event_group_set_bits(void*g,unsigned bits){assert(g==nan_event_group);event_bits|=bits;}
static wifi_netif_driver_t esp_netif_get_io_driver(void*n){assert(n);return n;}
static void esp_netif_receive(void){}
static esp_err_t esp_wifi_register_if_rxcb(void*d,void(*receive)(void),void*n){assert(d==n&&receive==esp_netif_receive);return rx_error;}
static void esp_wifi_nan_get_ipv6_linklocal_from_mac(ip6_addr_t*out,const uint8_t*mac){assert(mac);memset(out,0,sizeof(*out));out->addr[2]=42;}
static esp_err_t esp_wifi_netif_set_static_neighbor(int iface,const uint8_t*mac,bool add){assert(!depth&&iface==WIFI_IF_NAN&&mac&&add);return ESP_OK;}
static void*os_zalloc(size_t size){return fail_event?NULL:calloc(1,size);}
static void os_free(void*p){free(p);}
static void nan_app_post_event(int id,void*data,size_t size){assert(!depth&&id==WIFI_EVENT_NDP_CONFIRM&&data&&size>=sizeof(wifi_event_ndp_confirm_t));++posts;}
static void observe_ndp(uint32_t identity,const esp32_mquickjs_wifi_nan_sdk_notice_t*n,void*opaque){
 assert(identity&&opaque==&notices);++notices;
 if(n->kind==ESP32_MQUICKJS_NAN_SDK_NDP_FAILED)failed_error=n->status;
 if(n->kind==ESP32_MQUICKJS_NAN_SDK_NDP_CLEANUP_FAILED)cleanup_error=n->status;
 if(n->kind==ESP32_MQUICKJS_NAN_SDK_NDP_ACCEPTED){
  assert(!depth&&n->ndp_id==7&&!memcmp(n->peer,peer,6));
  assert(n->size==sizeof(esp32_mquickjs_wifi_nan_sdk_ndp_data_t));
  const esp32_mquickjs_wifi_nan_sdk_ndp_data_t*d=n->data;
  assert(d->ssi_len==3&&d->ssi[0]==1&&d->ssi[2]==3&&d->peer_ndi[5]==8&&d->own_ndi[5]==9);
  ++accepted;
 }
}
static void nan_app_ndp_confirm_cb(uint8_t,struct ndp_cb_peer_info*,uint8_t[6],uint8_t[8]);
static void deliver_confirm(void){
 uint8_t payload[3]={1,2,3},own[6]={2,3,4,5,6,9},ipv6[8]={0};
 struct ndp_cb_peer_info info={.ndp_id=7,.peer_nmi={2,3,4,5,6,7},.peer_ndi={2,3,4,5,6,8},.ssi=payload,.ssi_len=3};
 nan_app_ndp_confirm_cb(NDP_STATUS_ACCEPTED,&info,own,ipv6);
}
