
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdatomic.h>
#define CONFIG_WPS_REGISTRAR 1
#define CONFIG_SAE 1
#define ETH_ALEN 6
#define ESP_OK 0
#define ESP_ERR_INVALID_STATE 0x103
#define ESP_ERR_NO_MEM 0x101
#define os_memcpy memcpy
struct eapol_state_machine{bool exit_sm_step_run;uint8_t addr[6];};
struct sta_info{struct sta_info *next;struct eapol_state_machine *eapol_sm;uint8_t addr[6];void *lock;atomic_bool remove_pending;};
struct hostapd_data{struct sta_info *sta_list;};
typedef void (*handler)(void*,void*);
struct queued{handler fn;void *high,*low;unsigned seconds,useconds;};
static struct hostapd_data h;
static struct sta_info peers[2];
static bool sems[2],locked,closing;
static unsigned activity,registrations,ticks,steps,deleted,errors;
static int register_error,action;
static uint32_t owner_id=1;
static struct queued last;
#define HOSTAPD_STA_LIST_LOCK(p) do{assert((p)==&h && !locked);locked=true;}while(0)
#define HOSTAPD_STA_LIST_UNLOCK(p) do{assert((p)==&h && locked);locked=false;}while(0)
bool current_task_is_wifi_task(void){return true;}
static void *esp32qjs_wps_ap_result_native_context(uint32_t id){return id==owner_id && !closing?&h:NULL;}
static void *esp32qjs_wps_ap_result_activity_enter(uint32_t id){void *p=esp32qjs_wps_ap_result_native_context(id);if(p)activity++;return p;}
static void esp32qjs_wps_ap_result_callback_leave(uint32_t id){assert(id==owner_id && activity);activity--;}
static uint32_t esp32qjs_wps_ap_result_identity(void *p){return p==&h?owner_id:0;}
static int esp32qjs_wps_ap_result_peer_error(uint32_t id,int error,const uint8_t *addr){assert(id==owner_id && error && addr);errors++;return 0;}
static int eloop_register_timeout(unsigned sec,unsigned usec,handler fn,void *high,void *low){assert(!locked);registrations++;if(register_error)return register_error;last=(struct queued){fn,high,low,sec,usec};return 0;}
static int eloop_cancel_timeout(handler fn,void *high,void *low){assert(fn && high && low);return 0;}
static bool os_semphr_take(void *p,unsigned wait){assert(locked && !wait);bool *s=p;if(!*s)return false;*s=false;return true;}
static void os_semphr_give(void *p){assert(!locked && !*(bool*)p);*(bool*)p=true;}
static void ap_free_sta(struct hostapd_data *hapd,struct sta_info *sta);
void esp32qjs_wps_ap_peer_release(void *context,void *peer);
static void eapol_sm_step_cb(void *ctx,void *arg);
