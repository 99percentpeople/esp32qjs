"""Deferred real Radio/native deauth transaction; SDK queue and association table injected.

Uses production lease acquire/release, generic operation retirement and the whole
AP deauth include. Does not prove RF delivery or execute fixtures during API work.
"""
import re
import unittest
from test_wifi_vendor_ie import vendor_code, COMPONENT
from test_wifi_config_controls import structure
from wireless_vm_fixture import CORE, extract
from test_wireless_control_regression import compile_run


class WiFiAPDeauth(unittest.TestCase):
    def test_current_mac_exact_owner_and_unconfirmed_dispatch_retention(self):
        source = (COMPONENT / 'src/modules/wifi_radio/esp32_mquickjs_wifi_radio.c').read_text()
        header = (COMPONENT / 'internal/esp32_mquickjs_wifi_radio.h').read_text()
        code = vendor_code('esp32c3/representative')
        extra = re.search(r'typedef enum \{[^}]*\} esp32_mquickjs_wifi_radio_operation_kind_t;', header).group(0)
        extra += structure(header, 'esp32_mquickjs_wifi_radio_operation_t')
        code = code.replace('static struct {\n    int lock;', extra + '\nstatic struct {\n    int lock;', 1)
        code = code.replace('struct {unsigned identity,lease_identity;} operation;', 'esp32_mquickjs_wifi_radio_operation_t operation;')
        code = code.replace('generation,next_lease_identity,', 'event_live,next_operation_identity,generation,next_lease_identity,', 1)
        code += BOUNDARIES
        for path, name in ((CORE / 'esp32_mquickjs_wireless_core.c', 'esp32_mquickjs_wireless_secure_zero'),
                           (COMPONENT / 'src/modules/wifi_radio/esp32_mquickjs_wifi_radio.c', 'wifi_radio_record_fault'),
                           (COMPONENT / 'src/modules/wifi_radio/esp32_mquickjs_wifi_radio.c', 'esp32_mquickjs_wifi_radio_end_operation')):
            code += extract(path.read_text(), name)
        native = (COMPONENT / 'src/modules/wifi_radio/esp32_mquickjs_wifi_ap_deauth.inc').read_text()
        # The real target compiler checks 32-bit ABI layout. Host executes the
        # same control flow with its native pointer width, never driver offsets.
        native = native.replace('_Static_assert(sizeof(wifi_ap_deauth_ipc_t) == 12, "review AP deauth IPC ABI");', '')
        compile_run(self, code + native + MAIN)


BOUNDARIES = r'''
#include <stdlib.h>
#include <stdatomic.h>
#define ESP_ERR_NOT_FOUND 261
#define ESP_ERR_INVALID_RESPONSE 264
#define MALLOC_CAP_INTERNAL 1
#define MALLOC_CAP_8BIT 2
#ifndef ESP_WIFI_MAX_CONN_NUM
#define ESP_WIFI_MAX_CONN_NUM 15
#endif
static bool on_wifi,fail_alloc,defer,before_dispatch_replace;
static unsigned allocations,frees,ipc_calls,lookups,deauths;
static int ipc_error,lookup_error,deauth_error;
static uint16_t current_aid=1,last_aid;
static uint8_t current_mac[6]={2,0,0,0,0,1},sent_mac[6];
bool current_task_is_wifi_task(void){return on_wifi;}
static void *heap_caps_calloc(size_t n,size_t size,int caps){assert(locks&&!critical&&caps==3);
 if(fail_alloc)return NULL;void *p=calloc(n,size);assert(p);allocations++;return p;}
static void heap_caps_free(void *p){assert(locks&&!critical&&p);frees++;free(p);}
static int esp_wifi_ap_get_sta_aid(const uint8_t mac[6],uint16_t *aid){assert(on_wifi&&!critical);lookups++;
 if(lookup_error)return lookup_error;if(memcmp(mac,current_mac,6))return ESP_ERR_NOT_FOUND;*aid=current_aid;return 0;}
static int esp_wifi_deauth_sta(uint16_t aid){assert(on_wifi&&!critical&&aid&&aid==current_aid);deauths++;
 last_aid=aid;memcpy(sent_mac,current_mac,6);return deauth_error;}
'''

MAIN = r'''
static wifi_ap_deauth_ipc_t *queued;
int esp_wifi_ipc_internal(wifi_ap_deauth_ipc_t *config,bool sync){assert(locks&&!critical&&sync&&config&&config->arg_size==0);ipc_calls++;
 if(defer){assert(!queued);queued=config;return ipc_error;}
 if(ipc_error)return ipc_error;
 if(before_dispatch_replace)current_mac[5]=2;
 on_wifi=true;int e=config->fn(config->arg);on_wifi=false;return e;}
static esp32_mquickjs_wifi_radio_lease_t ap_lease;
static bool requested,unknown;
static const char *stage;
static const uint8_t address[6]={2,0,0,0,0,1};
static int request(void){return esp32_mquickjs_wifi_radio_ap_deauth(&ap_lease,address,&requested,&unknown,&stage);}
static void setup(void){assert(!s_ap_deauth&&!queued&&allocations==frees);reset_vendor();memset(&ap_lease,0,sizeof(ap_lease));
 s_radio.effective_mode=s_radio.event_live=WIFI_MODE_AP;s_radio.next_operation_identity=1;
 wifi_radio_operation_lock();assert(!wifi_radio_acquire_locked(ESP32_MQUICKJS_WIFI_RADIO_CLIENT_WIFI_AP,WIFI_MODE_AP,&ap_lease));wifi_radio_operation_unlock();
 on_wifi=fail_alloc=defer=before_dispatch_replace=false;ipc_error=lookup_error=deauth_error=0;
 ipc_calls=lookups=deauths=0;current_aid=1;memcpy(current_mac,address,6);}
static void late(void){assert(queued&&!locks&&!critical);wifi_ap_deauth_ipc_t *q=queued;queued=NULL;
 on_wifi=true;q->fn(q->arg);on_wifi=false;}
int main(void){setup();
 assert(!request()&&requested&&!unknown&&lookups==1&&deauths==1&&!memcmp(sent_mac,address,6)&&!s_radio.operation.identity);
 setup();before_dispatch_replace=true;assert(!request()&&!requested&&!deauths); /* Old AID now belongs to another MAC. */
 setup();current_aid=3;assert(!request()&&requested&&last_aid==3); /* Look up at execution, never reuse a caller's old AID. */
 setup();current_aid=0;assert(request()==ESP_ERR_INVALID_RESPONSE&&!deauths);
 current_aid=ESP_WIFI_MAX_CONN_NUM+1;assert(request()==ESP_ERR_INVALID_RESPONSE&&!deauths);
 setup();lookup_error=77;assert(request()==77&&!requested&&!deauths&&!strcmp(stage,"deauth-address"));
 setup();deauth_error=88;assert(request()==88&&!requested&&deauths==1&&!strcmp(stage,"deauth-request"));
 setup();fail_alloc=true;assert(request()==ESP_ERR_NO_MEM&&!ipc_calls&&!s_radio.operation.identity);
 setup();s_radio.next_operation_identity=0;assert(request()==ESP_ERR_NO_MEM&&!ipc_calls);
 setup();ap_lease.generation++;assert(request()==ESP_ERR_INVALID_STATE&&!ipc_calls);
 setup();s_radio.lifecycle.identity=999;assert(request()==ESP_ERR_INVALID_STATE&&!ipc_calls);
 setup();ipc_error=ESP_ERR_NO_MEM;assert(request()==ESP_ERR_NO_MEM&&!unknown&&allocations==frees);
 setup();defer=true;ipc_error=77;assert(request()==77&&unknown&&!requested&&s_radio.operation.identity);
 esp32_mquickjs_wifi_radio_operation_t old=s_radio.operation;esp32_mquickjs_wifi_radio_end_operation(&old);
 assert(old.identity&&s_radio.operation.identity);
 wifi_radio_operation_lock();wifi_radio_release_locked(&ap_lease);assert(wifi_radio_lease_valid(&ap_lease));wifi_radio_operation_unlock();
 bool pending=false;assert(!esp32_mquickjs_wifi_radio_ap_deauth_poll(&pending)&&pending&&allocations==frees+1);
 assert(request()==ESP_ERR_INVALID_STATE&&ipc_calls==1);late();
 assert(esp32_mquickjs_wifi_radio_ap_deauth_poll(&pending)&&!pending&&!s_radio.operation.identity&&!s_radio.fault_stage);
 assert(ipc_calls==1&&deauths==1&&allocations==frees);
 assert(!esp32_mquickjs_wifi_radio_ap_deauth_poll(NULL));
 /* A stale token and a late receipt cannot release a later operation or
  * clear a fault from another source. */
 defer=false;ipc_error=0;assert(!request());esp32_mquickjs_wifi_radio_end_operation(&old);assert(!s_radio.operation.identity);
 setup();defer=true;ipc_error=77;assert(request()==77);s_radio.fault_stage="unrelated-fault";late();
 assert(esp32_mquickjs_wifi_radio_ap_deauth_poll(NULL)&&!strcmp(s_radio.fault_stage,"unrelated-fault"));
 assert(!s_ap_deauth&&allocations==frees&&!critical&&!locks);return 0;
}
'''
