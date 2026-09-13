
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdatomic.h>
#define CONFIG_WPS_REGISTRAR 1
#define CONFIG_SAE 1
#define ESP_OK 0
#define ESP_ERR_NO_MEM 0x101
#define ESP_ERR_INVALID_STATE 0x103
#define ETH_ALEN 6
#define WLAN_REASON_IEEE_802_1X_AUTH_FAILED 23
#define os_memcpy memcpy
struct sta_info {struct sta_info *next;void *eapol_sm,*lock;atomic_bool remove_pending;uint8_t addr[6];};
struct hostapd_data {struct sta_info *sta_list;};
static struct hostapd_data h;
static bool available=true,table_lock;
static unsigned identity=11,activity,removed,deauths;
static int error,reported;
struct queued{void(*fn)(void*,void*);void *a,*b;};static struct queued last;
#define HOSTAPD_STA_LIST_LOCK(p) do{(void)p;assert(!table_lock);table_lock=true;}while(0)
#define HOSTAPD_STA_LIST_UNLOCK(p) do{(void)p;assert(table_lock);table_lock=false;}while(0)
bool current_task_is_wifi_task(void){return true;}
static uint32_t esp32qjs_wps_ap_result_identity(const void *p){return p==&h?identity:0;}
static void *esp32qjs_wps_ap_result_native_context(uint32_t id){return id==identity?&h:NULL;}
static void *esp32qjs_wps_ap_result_activity_enter(uint32_t id){if(id!=identity)return NULL;activity++;return &h;}
static void esp32qjs_wps_ap_result_callback_leave(uint32_t id){assert(id==identity && activity);activity--;}
static void esp32qjs_wps_ap_result_peer_error(uint32_t id,int e,const uint8_t *p){assert(id==identity && p && !table_lock);reported=e;}
static void *hostapd_get_hapd_data(void){return &h;}
static void esp32qjs_wps_ap_eapol_stop_peer(void *p){(void)p;}
static bool os_semphr_take(void *p,int wait){(void)wait;bool ok=*(bool*)p;if(ok)*(bool*)p=false;return ok;}
static void ap_free_sta(struct hostapd_data*,struct sta_info*);
static void esp32qjs_wps_ap_peer_release(void*,void*);
static int esp_wifi_ap_deauth_internal(uint8_t *addr,uint32_t reason){assert(addr && reason==23);deauths++;return error;}
static int eloop_register_timeout(unsigned sec,unsigned usec,void(*fn)(void*,void*),void *a,void *b){(void)sec;(void)usec;last=(struct queued){fn,a,b};return error;}
static int eloop_cancel_timeout(void(*fn)(void*,void*),void *a,void *b){(void)fn;(void)a;(void)b;return 0;}
