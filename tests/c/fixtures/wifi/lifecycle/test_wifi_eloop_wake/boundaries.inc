
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
typedef struct {int(*fn)(void*);void*arg;unsigned arg_size;}wifi_ipc_config_t;
static struct {int timeout,eloop_timer;}eloop;
static void *eloop_data_lock=(void*)1;
static bool running=true,empty,locked,cancel_during_post,destroy_during_post;
static unsigned posts,arms,disarms;
static int ipc_error;
#define ELOOP_LOCK() do{assert(!locked);locked=true;}while(0)
#define ELOOP_UNLOCK() do{assert(locked);locked=false;}while(0)
static bool eloop_is_running(void){assert(locked);return running;}
static bool dl_list_empty(void*p){assert(p==&eloop.timeout&&locked);return empty;}
static void os_timer_disarm(void*p){assert(p==&eloop.eloop_timer&&locked);disarms++;}
static void os_timer_arm(void*p,unsigned ms,int repeat){assert(p==&eloop.eloop_timer&&locked&&ms==10&&!repeat);arms++;}
static int eloop_run_wrapper(void*p){assert(!p);return 0;}
static int esp_wifi_ipc_internal(wifi_ipc_config_t*config,bool sync){
 assert(!locked&&!sync&&config->fn==eloop_run_wrapper&&!config->arg&&!config->arg_size);posts++;
 if(cancel_during_post)empty=true;
 if(destroy_during_post)running=false;
 return ipc_error;
}
