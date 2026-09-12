"""Deferred exact peer-delay production helper tests; AST only in this wave."""
from pathlib import Path
import sys
import unittest
from test_wireless_control_regression import compile_run
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'scripts'))
from patch_idf_wps_peer_delays import DELAYS

class WpsPeerDelays(unittest.TestCase):
    def run_case(self,main):
        compile_run(self,TYPES+DELAYS+BOUNDARIES+main)

    def test_old_ticket_cannot_delete_replacement_at_same_mac_or_address(self):
        self.run_case(r'''
int main(void){
 setup();assert(esp32qjs_wps_ap_peer_remove_locked(&h,&a.sta)==0);
 struct queued old=last;esp32qjs_wps_peer_cancel(&a.sta,0);
 memset(&a,0,sizeof(a));h.sta_list=&a.sta;a.sta.lock=&available;
 assert(esp32qjs_wps_ap_peer_remove_locked(&h,&a.sta)==0);
 old.fn(old.a,old.b);assert(!removed);
 last.fn(last.a,last.b);assert(removed==1 && !activity && !table_lock);
 return 0;
}
''')

    def test_removal_oom_retains_obligation_and_close_retries_busy_peer(self):
        self.run_case(r'''
int main(void){
 setup();error=-81;
 assert(esp32qjs_wps_ap_peer_remove_locked(&h,&a.sta)==-81);
 assert(a.remove_needed && a.error==-81 && !a.tickets[0]);
 available=false;assert(esp32qjs_wps_ap_peer_delays_stop(&h,identity)==ESP_ERR_INVALID_STATE);
 assert(a.sta.remove_pending && !removed);
 available=true;assert(esp32qjs_wps_ap_peer_delays_stop(&h,identity)==0);
 assert(removed==1 && !table_lock);return 0;
}
''')

    def test_deauth_failure_retains_raw_error_without_removing_other_peer(self):
        self.run_case(r'''
int main(void){
 setup();assert(esp32qjs_wps_peer_schedule_locked(&h,&a.sta,1)==0);
 error=-95;last.fn(last.a,last.b);
 assert(a.error==-95 && reported==-95 && deauths==1 && !removed && available);
 assert(!a.tickets[1] && !activity);return 0;
}
''')

    def test_close_revokes_deauth_and_identity_exhaustion_never_wraps(self):
        self.run_case(r'''
int main(void){
 setup();assert(esp32qjs_wps_peer_schedule_locked(&h,&a.sta,1)==0);struct queued old=last;
 assert(esp32qjs_wps_ap_peer_delays_stop(&h,identity)==0);
 old.fn(old.a,old.b);assert(!deauths && !removed);
 esp32qjs_wps_peer_last_ticket=UINT64_MAX;
 assert(esp32qjs_wps_ap_peer_remove_locked(&h,&a.sta)==ESP_ERR_NO_MEM);
 assert(a.remove_needed && esp32qjs_wps_peer_last_ticket==UINT64_MAX);return 0;
}
''')

TYPES=r'''
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
'''
BOUNDARIES=r'''
static struct esp32qjs_wps_peer_record a;
static void setup(void){h.sta_list=&a.sta;a.sta.lock=&available;}
static void ap_free_sta(struct hostapd_data *p,struct sta_info *s){assert(!table_lock && p==&h && s==&a.sta);h.sta_list=s->next;removed++;}
static void esp32qjs_wps_ap_peer_release(void *p,void *s){assert(p==&h && s==&a.sta);if(a.sta.remove_pending)ap_free_sta(p,s);else available=true;}
'''
