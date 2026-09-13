
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include "esp_err.h"
typedef struct {uint8_t ndp_id,peer_mac[6];} wifi_nan_datapath_resp_t;
struct ndl_info {uint32_t device_caps;uint8_t peer_nmi[6];};
static struct ndl_info link={.device_caps=8,.peer_nmi={2,3,4,5,6,7}};
typedef struct {union{struct{uint32_t addr[4];}ip6;}u_addr;} ip_addr_t;
static unsigned depth,submits;static uint8_t null_mac[6];static struct{unsigned event;}s_nan_ctx={16};
#define NAN_DATA_LOCK() do{assert(!depth);++depth;}while(0)
#define NAN_DATA_UNLOCK() do{assert(depth==1);--depth;}while(0)
#define NAN_CAPS_NDPE_ATTR 8
#define NDP_INDICATION 16
#define WIFI_IF_NAN 3
#define MACADDR_EQUAL(a,b) (!memcmp(a,b,6))
#define MACADDR_COPY(a,b) memcpy(a,b,6)
#define ESP_LOGE(...) ((void)0)
static struct ndl_info *nan_find_ndl(uint8_t id,void *peer){(void)peer;return id==1?&link:NULL;}
static int esp_wifi_get_mac(int interface,uint8_t *mac){(void)interface;(void)mac;assert(depth==1);return -72;}
static void esp_wifi_nan_get_ipv6_linklocal_from_mac(void *out,void *mac){(void)out;(void)mac;assert(0);}
static int esp_nan_internal_datapath_resp(void *r,void *v){(void)r;(void)v;submits++;return 0;}
