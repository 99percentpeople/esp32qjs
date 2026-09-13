
#include <assert.h>
#include <stdbool.h>
#include <string.h>
static void *nan_event_group,*s_nan_data_lock;
static struct{unsigned state;void *netif;}s_nan_ctx;
static int s_nan_secure_dp_funcs;
static unsigned calls,fail_at,live,registered;
#define NAN_DATA_LOCK() ((void)0)
#define NAN_DATA_UNLOCK() ((void)0)
#define ESP_LOGE(...) ((void)0)
static void *allocate(void){++calls;if(calls==fail_at)return NULL;++live;return (void *)(unsigned long)calls;}
static void *os_event_group_create(void){return allocate();}
static void *os_recursive_mutex_create(void){return allocate();}
static void os_event_group_delete(void *p){assert(p&&live);--live;}
static void os_mutex_delete(void *p){assert(p&&live);--live;}
static void esp_nan_internal_register_secure_dp_funcs(void *p){registered=p!=NULL;}
static void nan_reset_service(unsigned id,bool all){assert(!id&&all);}
static void nan_reset_ndl(unsigned id,bool all){assert(!id&&all);}
