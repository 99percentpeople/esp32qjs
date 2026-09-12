"""Deferred production EAPOL timer identity and peer-lock regressions.

Implementation wave: AST only. Production TIMERS and PEER_RELEASE are compiled
with event-loop, AP owner and semaphore boundaries when the Wi-Fi suite runs.
No replacement EAP state machine or claim of complete SDK/RF qualification.
"""
from pathlib import Path
import sys
import unittest
from test_wireless_control_regression import compile_run

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts'))
from patch_idf_wps_eapol import TIMERS, PEER_RELEASE


class WpsEapolTimers(unittest.TestCase):
    def run_case(self, main):
        compile_run(self, TYPES + TIMERS + PEER_RELEASE + BOUNDARIES + main)

    def test_consumed_cancelled_and_reused_address_tickets_cannot_dispatch(self):
        self.run_case(r'''
int main(void){
 setup();assert(esp32qjs_wps_eapol_arm(&a.sm,0)==0);
 struct queued old=last;
 old.fn(old.high,old.low);assert(ticks==1 && !activity && !locked);
 assert(a.timers[0] && a.timers[0]!=(uint32_t)(uintptr_t)old.low);
 old.fn(old.high,old.low);assert(ticks==1);
 struct queued current=last;
 esp32qjs_wps_eapol_cancel(&a.sm,0);current.fn(current.high,current.low);assert(ticks==1);
 assert(esp32qjs_wps_eapol_arm(&a.sm,1)==0);old=last;
 esp32qjs_wps_eapol_stop_record(&a.sm);
 memset(&a,0,sizeof(a));a.identity=owner_id;peers[0].eapol_sm=&a.sm;
 assert(esp32qjs_wps_eapol_arm(&a.sm,1)==0);current=last;
 old.fn(old.high,old.low);assert(!steps);
 current.fn(current.high,current.low);assert(steps==1 && !a.timers[1]);
 old=current;owner_id++;a.identity=owner_id;
 old.fn(old.high,old.low);assert(steps==1 && !activity);
 return 0;
}
''')

    def test_sae_busy_retry_is_numeric_and_removed_peer_cannot_be_touched(self):
        self.run_case(r'''
int main(void){
 setup();assert(esp32qjs_wps_eapol_arm(&a.sm,1)==0);struct queued old=last;
 sems[0]=false;old.fn(old.high,old.low);
 assert(!steps && !ticks && last.seconds==0 && last.useconds==1000);
 assert(last.high==old.high && last.low==old.low && a.timers[1]);
 struct queued retry=last;h.sta_list=&peers[1];peers[0].eapol_sm=NULL;
 memset(&a,0xa5,sizeof(a)); /* Freed storage must not be looked up. */
 retry.fn(retry.high,retry.low);assert(!steps && !activity && !locked);
 return 0;
}
''')

    def test_whole_callback_pins_peer_and_deferred_delete_consumes_lock(self):
        self.run_case(r'''
int main(void){
 setup();action=1;assert(esp32qjs_wps_eapol_arm(&a.sm,1)==0);
 last.fn(last.high,last.low);
 assert(steps==1 && deleted==1 && !peers[0].eapol_sm && !activity && !locked);
 assert(!sems[0]); /* ap_free_sta consumes the held semaphore. */
 assert(peers[1].eapol_sm==&b.sm && !b.stopped);
 return 0;
}
''')

    def test_busy_retry_allocation_failure_stops_exact_peer_and_preserves_other(self):
        self.run_case(r'''
int main(void){
 setup();assert(esp32qjs_wps_eapol_arm(&b.sm,0)==0);
 assert(esp32qjs_wps_eapol_arm(&a.sm,0)==0);
 assert(esp32qjs_wps_eapol_arm(&a.sm,1)==0);struct queued pending=last;
 sems[0]=false;register_error=-91;
 pending.fn(pending.high,pending.low);
 assert(a.stopped && a.error==-91 && !a.timers[0] && !a.timers[1]);
 assert(!b.stopped && b.timers[0] && errors==1 && !activity && !locked);
 return 0;
}
''')

    def test_timer_oom_and_exhaustion_fail_only_this_peer_and_stop_revokes_both(self):
        self.run_case(r'''
int main(void){
 setup();assert(esp32qjs_wps_eapol_arm(&b.sm,0)==0);struct queued other=last;
 register_error=-73;assert(esp32qjs_wps_eapol_arm(&a.sm,1)==-73);
 assert(a.stopped && a.error==-73 && a.sm.exit_sm_step_run && errors==1);
 assert(!b.stopped && b.timers[0]);register_error=0;
 other.fn(other.high,other.low);assert(ticks==1);
 esp32qjs_wps_eapol_last_timer=UINT32_MAX;
 assert(esp32qjs_wps_eapol_arm(&b.sm,1)==ESP_ERR_NO_MEM);
 assert(b.error==ESP_ERR_NO_MEM && b.stopped && !b.timers[0]);
 other.fn(other.high,other.low);assert(ticks==1 && !activity);
 assert(esp32qjs_wps_ap_eapol_stop(&h,owner_id)==ESP_OK);
 assert(esp32qjs_wps_ap_eapol_stop(&h,owner_id+1)==ESP_ERR_INVALID_STATE);
 return 0;
}
''')

    def test_pending_step_coalesces_and_close_drops_old_callbacks(self):
        self.run_case(r'''
int main(void){
 setup();assert(esp32qjs_wps_eapol_arm(&a.sm,1)==0);struct queued old=last;unsigned before=registrations;
 assert(esp32qjs_wps_eapol_arm(&a.sm,1)==0 && registrations==before);
 closing=true;assert(esp32qjs_wps_ap_eapol_stop(&h,owner_id)==0);
 old.fn(old.high,old.low);assert(!steps && !activity && a.stopped && b.stopped);
 assert(esp32qjs_wps_eapol_arm(&a.sm,0)==ESP_ERR_INVALID_STATE);
 return 0;
}
''')


TYPES = r'''
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
'''

BOUNDARIES = r'''
static struct esp32qjs_wps_eapol_record a,b;
static void setup(void){
 memset(&a,0,sizeof(a));memset(&b,0,sizeof(b));memset(peers,0,sizeof(peers));
 a.identity=b.identity=owner_id;peers[0].next=&peers[1];h.sta_list=peers;
 peers[0].eapol_sm=&a.sm;peers[1].eapol_sm=&b.sm;
 for(unsigned i=0;i<2;i++){sems[i]=true;peers[i].lock=&sems[i];peers[i].addr[0]=i+1;atomic_init(&peers[i].remove_pending,false);}
}
static void eapol_port_timers_tick(void *ctx,void *arg){
 assert(!ctx && activity && !locked);ticks++;
 struct eapol_state_machine *sm=arg;assert((sm==&a.sm && !sems[0]) || (sm==&b.sm && !sems[1]));
 esp32qjs_wps_eapol_arm(sm,0);
}
static void eapol_sm_step_cb(void *ctx,void *arg){
 assert(ctx==&a.sm && !arg && !sems[0] && activity && !locked);steps++;
 if(action==1)atomic_store(&peers[0].remove_pending,true);
}
static void ap_free_sta(struct hostapd_data *hapd,struct sta_info *sta){
 assert(hapd==&h && sta==&peers[0] && !sems[0] && activity && !locked);
 h.sta_list=sta->next;esp32qjs_wps_eapol_stop_record(sta->eapol_sm);sta->eapol_sm=NULL;deleted++;
}
'''
