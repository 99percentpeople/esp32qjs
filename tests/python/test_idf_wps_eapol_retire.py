"""Deferred production retirement queue scheduling tests; AST only in this wave."""
import os
from pathlib import Path
import re
import sys
import unittest
from test_wireless_control_regression import compile_run

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts'))
from patch_idf_wps_registrar import patch_source
from patch_idf_wps_eapol_retire import RETIRE


class WpsEapolRetire(unittest.TestCase):
    def run_case(self, main):
        sdk = os.environ.get('IDF_PATH')
        if not sdk:
            self.skipTest('Set IDF_PATH to the reviewed ESP-IDF')
        relative = 'src/eapol_auth/eapol_auth_sm.c'
        source = patch_source(relative, (Path(sdk) / 'components/wpa_supplicant' / relative).read_bytes()).decode()
        record = re.search(r'struct esp32qjs_wps_eapol_record \{.*?\n\};', source, re.S).group()
        compile_run(self, TYPES + record + HELPERS + RETIRE + BOUNDARIES + main)

    def test_foreign_unlink_publishes_owner_and_native_wake_destroys(self):
        self.run_case(r'''
int main(void){
 struct esp32qjs_wps_eapol_record *a=make_record();struct sta_info peer={&a->sm};
 native=false;esp32qjs_wps_ap_eapol_detach_peer(&peer);
 assert(!peer.eapol_sm && !destroyed && esp32qjs_wps_eapol_children==1);
 assert(!esp32qjs_wps_ap_eapol_drained(identity));
 native=true;queued(queued_arg,NULL);
 assert(destroyed==1 && !esp32qjs_wps_eapol_children);
 assert(esp32qjs_wps_ap_eapol_drained(identity) && !critical);
 return 0;
}
''')

    def test_callback_reference_retains_removed_storage_until_outer_dispatch_returns(self):
        self.run_case(r'''
int main(void){
 struct esp32qjs_wps_eapol_record *a=make_record();
 assert(esp32qjs_wps_eapol_ref(&a->sm));assert(esp32qjs_wps_eapol_ref(&a->sm));
 eapol_auth_free(&a->sm);eapol_auth_free(&a->sm);
 assert(!esp32qjs_wps_eapol_ref(&a->sm) && esp32qjs_wps_eapol_is_retired(&a->sm));
 queued(queued_arg,NULL);assert(!destroyed && !esp32qjs_wps_ap_eapol_drained(identity));
 esp32qjs_wps_eapol_unref(&a->sm);queued(queued_arg,NULL);assert(!destroyed);
 esp32qjs_wps_eapol_unref(&a->sm);queued(queued_arg,NULL);
 assert(destroyed==1 && esp32qjs_wps_ap_eapol_drained(identity));return 0;
}
''')

    def test_wake_submission_interleaving_prevents_early_parent_release(self):
        self.run_case(r'''
int main(void){
 struct esp32qjs_wps_eapol_record *a=make_record();
 schedule_interleave=true;native=false;eapol_auth_free(&a->sm);
 assert(destroyed==1 && !esp32qjs_wps_eapol_producers);
 native=true;assert(esp32qjs_wps_ap_eapol_drained(identity));return 0;
}
''')

    def test_wake_oom_retains_queue_explicit_close_drains_and_old_identity_is_inert(self):
        self.run_case(r'''
int main(void){
 struct esp32qjs_wps_eapol_record *a=make_record();wake_error=-77;
 eapol_auth_free(&a->sm);assert(!destroyed && !esp32qjs_wps_eapol_wake_identity);
 assert(!esp32qjs_wps_ap_eapol_drained(identity));
 assert(esp32qjs_wps_ap_eapol_drain(&context,identity+1)==ESP_ERR_INVALID_STATE);
 assert(esp32qjs_wps_ap_eapol_drain(&context,identity)==0);
 assert(destroyed==1 && esp32qjs_wps_ap_eapol_drained(identity));
 uint32_t old=identity++;wake_error=0;a=make_record();eapol_auth_free(&a->sm);
 esp32qjs_wps_eapol_wake((void*)(uintptr_t)old,NULL);
 assert(destroyed==1 && esp32qjs_wps_eapol_wake_identity==identity);
 queued(queued_arg,NULL);assert(destroyed==2 && esp32qjs_wps_ap_eapol_drained(identity));
 return 0;
}
''')


TYPES = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#define ESP_OK 0
#define ESP_ERR_INVALID_STATE 0x103
typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
static int critical,context,destroyed,wake_error;
static bool native=true,schedule_interleave;
static uint32_t identity=7;
#define portENTER_CRITICAL(p) do{(void)(p);assert(!critical);critical=1;}while(0)
#define portEXIT_CRITICAL(p) do{(void)(p);assert(critical);critical=0;}while(0)
struct eapol_state_machine {int unused;};
struct sta_info {struct eapol_state_machine *eapol_sm;};
static void (*queued)(void*,void*);
static void *queued_arg;
static bool current_task_is_wifi_task(void){return native;}
static uint32_t esp32qjs_wps_ap_result_identity(const void *p){return native && p==&context?identity:0;}
static void *hostapd_get_hapd_data(void){return &context;}
static int eloop_register_timeout(unsigned,unsigned,void(*)(void*,void*),void*,void*);
static int eloop_cancel_timeout(void(*)(void*,void*),void*,void*);
'''
HELPERS = r'''
static struct esp32qjs_wps_eapol_record *esp32qjs_wps_eapol_record(struct eapol_state_machine *sm){return (void*)sm;}
static void esp32qjs_wps_eapol_stop_record(struct eapol_state_machine *sm){esp32qjs_wps_eapol_record(sm)->stopped=true;}
'''
BOUNDARIES = r'''
static struct esp32qjs_wps_eapol_record *make_record(void){
 struct esp32qjs_wps_eapol_record *p=calloc(1,sizeof(*p));assert(p);
 p->identity=identity;assert(esp32qjs_wps_eapol_adopt(identity));return p;
}
static void esp32qjs_wps_eapol_destroy(struct eapol_state_machine *sm){
 assert(native && !critical && esp32qjs_wps_eapol_children);
 assert(!esp32qjs_wps_ap_eapol_drained(identity));
 destroyed++;free(sm);
}
static int eloop_register_timeout(unsigned sec,unsigned us,void(*fn)(void*,void*),void*a,void*b){
 (void)sec;(void)us;assert(!b && !critical);queued=fn;queued_arg=a;
 if(schedule_interleave){
  schedule_interleave=false;bool was=native;native=true;
  fn(a,b);assert(destroyed==1 && !esp32qjs_wps_ap_eapol_drained(identity));native=was;
 }
 return wake_error;
}
static int eloop_cancel_timeout(void(*fn)(void*,void*),void*a,void*b){
 (void)fn;(void)a;(void)b;assert(!critical);return 0;
}
'''
