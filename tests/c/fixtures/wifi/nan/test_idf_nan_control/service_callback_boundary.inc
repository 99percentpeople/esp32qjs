
#include <stdlib.h>
#define NAN_DATA_LOCK() ((void)0)
#define NAN_DATA_UNLOCK() ((void)0)
#define MACADDR_EQUAL(a,b) (!memcmp(a,b,6))
#define MACADDR_COPY(a,b) memcpy(a,b,6)
#define ESP_LOGE(...) ((void)0)
#define ESP_LOG_BUFFER_HEXDUMP(...) ((void)0)
#define WIFI_EVENT_NAN_REPLIED 3
struct nan_cb_peer_info {uint8_t peer_svc_id,peer_mac[6],*ssi;uint16_t ssi_len;uint32_t device_caps;};
struct peer_svc_info {uint8_t svc_id,own_svc_id,peer_nmi[6];uint32_t device_caps;};
typedef struct {uint8_t publish_id,subscribe_id,sub_if_mac[6];uint32_t reserved_1,reserved_2;uint16_t ssi_len;uint8_t ssi[];} wifi_event_nan_replied_t;
static struct peer_svc_info peer;
static bool fail_allocation,free_entered,free_continue;
static unsigned posted;
static pthread_mutex_t free_mutex=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t free_condition=PTHREAD_COND_INITIALIZER;
static struct peer_svc_info *nan_find_peer_svc(uint8_t a,uint8_t b,const uint8_t *c){(void)a;(void)b;(void)c;return &peer;}
static void nan_record_peer_svc(uint8_t a,uint8_t b,const uint8_t*c,uint32_t d){(void)a;(void)b;(void)c;(void)d;}
static void *os_zalloc(size_t size){return fail_allocation?NULL:calloc(1,size);}
static void nan_app_post_event(int event,void *data,size_t size){assert(event==3&&data&&size);++posted;}
static void os_free(void *p){
 pthread_mutex_lock(&free_mutex);free_entered=true;pthread_cond_broadcast(&free_condition);
 while(!free_continue)pthread_cond_wait(&free_condition,&free_mutex);
 pthread_mutex_unlock(&free_mutex);free(p);
}
