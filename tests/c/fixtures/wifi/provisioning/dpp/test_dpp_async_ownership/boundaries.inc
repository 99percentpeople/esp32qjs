
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
typedef int esp_err_t;
typedef void (*eloop_timeout_handler)(void *,void *);
#define ESP_OK 0
#define ESP_ERR_NOT_FINISHED 1
#define ESP_ERR_INVALID_STATE 2
#define ELOOP_ALL_CTX ((void *)-1)
struct action_rx_param {unsigned frm_len;unsigned char bytes[];};
static struct {bool dpp_deinit_pending;}s_dpp_ctx;
static atomic_bool s_dpp_init_done=true,dpp_shutting_down;
static int locked,live,fail_at,allocations,calls,bootstrap_calls;
static int dpp_api_lock(void){locked++;return 0;}
static int dpp_api_unlock(void){assert(locked);locked--;return 0;}
static void *os_zalloc(size_t n){if(++allocations==fail_at)return NULL;void *p=calloc(1,n);assert(p);live++;return p;}
static void os_free(void *p){if(p){live--;free(p);}}
static void bin_clear_free(void *p,size_t n){if(p){volatile unsigned char *q=p;for(size_t i=0;i<n;i++)q[i]=0;os_free(p);}}
struct queued {eloop_timeout_handler fn;void *a,*b;};
static struct queued queue[64];
static bool queue_fail;
static int eloop_register_timeout(unsigned s,unsigned u,eloop_timeout_handler fn,void*a,void*b){
 (void)s;(void)u;assert(locked);if(queue_fail)return -1;
 for(unsigned i=0;i<64;i++)if(!queue[i].fn){queue[i]=(struct queued){fn,a,b};return 0;}return -1;
}
static int eloop_cancel_timeout(eloop_timeout_handler fn,void*a,void*b){
 assert(locked);for(unsigned i=0;i<64;i++)if(queue[i].fn==fn&&queue[i].a==a&&queue[i].b==b){queue[i].fn=NULL;return 1;}return 0;
}
static struct queued detach_first(void){for(unsigned i=0;i<64;i++)if(queue[i].fn){struct queued q=queue[i];queue[i].fn=NULL;return q;}abort();}
