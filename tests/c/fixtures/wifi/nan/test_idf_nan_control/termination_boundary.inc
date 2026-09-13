
#include <stdlib.h>
static struct{void *nan_netif;unsigned event;}s_nan_ctx;
typedef struct{uint8_t reason,ndp_id,init_ndi[6];}wifi_event_ndp_terminated_t;
static unsigned bits,notifications,resets;static void *nan_event_group=(void *)1;
#define NDP_INDICATION 16
#define NDP_TERMINATED 64
#define NAN_DATA_LOCK() ((void)0)
#define NAN_DATA_UNLOCK() ((void)0)
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGE(...) ((void)0)
#define WIFI_EVENT 1
#define WIFI_EVENT_NDP_TERMINATED 2
#define MACADDR_COPY(a,b) memcpy(a,b,6)
static bool nan_is_datapath_active(void){return false;}
static void esp_netif_action_disconnected(void*a,int b,int c,void*d){(void)a;(void)b;(void)c;(void)d;}
static void nan_reset_ndl(uint8_t id,bool all){assert(id==9&&!all);resets++;}
static void *os_zalloc(size_t n){(void)n;return NULL;}
static void os_free(void *p){free(p);}
static void nan_app_post_event(int id,void*p,size_t n){(void)id;(void)p;(void)n;assert(0);}
static void os_event_group_set_bits(void *group,unsigned value){assert(group==nan_event_group);bits|=value;}
