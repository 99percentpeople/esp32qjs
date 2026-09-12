"""Deferred AP pre-start production helper ownership and rollback regressions.

The actual helper executes against SDK mode/allocator/return boundaries. This
does not replace target linkage or native ioctl/RF validation. Do not execute
until the consolidated Wi-Fi test phase.
"""
import re
import unittest
from pathlib import Path
from test_wifi_config_controls import sdk_types, structure
from test_wireless_control_regression import compile_run
from wireless_vm_fixture import extract

BASE = Path(__file__).resolve().parents[2] / 'components/esp32_mquickjs'


def source():
    header = (BASE / 'internal/esp32_mquickjs_wifi_ap_prestart.h').read_text()
    code = (BASE / 'src/modules/wifi/esp32_mquickjs_wifi_ap_prestart.c').read_text()
    code = re.sub(r'^#include[^\n]*\n', '', code, flags=re.M)
    result = structure(header, 'esp32_mquickjs_wifi_ap_prestart_result_t')
    policy = extract((BASE / 'src/modules/wifi_radio/esp32_mquickjs_wifi_radio.c').read_text(),
                     'esp32_mquickjs_wifi_radio_pmf_disable_allowed')
    return PREFIX + sdk_types('esp32c5/representative') + result + BOUNDARIES + policy + code + SDK


class WiFiAPPrestart(unittest.TestCase):
    def test_capture_exact_identity_and_no_native_use_after_release(self):
        compile_run(self, source() + r'''
int main(void){
 wifi_config_t requested={0},previous={0};requested.ap.ssid[0]='n';previous.ap.ssid[0]='o';
 fail_allocation=true;
 assert(esp32_mquickjs_wifi_ap_prestart_prepare(9,41,&requested,&previous,matches)==ESP_ERR_NO_MEM);
 assert(!s_ap_prestart&&!native_allocations&&!mode_creates&&!setters);
 fail_allocation=false;assert(!esp32_mquickjs_wifi_ap_prestart_prepare(9,41,&requested,&previous,matches));
 assert(esp32_mquickjs_wifi_ap_prestart_prepare(9,42,&requested,&previous,matches)==ESP_ERR_INVALID_STATE);
 assert(esp32_mquickjs_wifi_ap_prestart_release(8,41)==ESP_ERR_INVALID_STATE);
 assert(esp32_mquickjs_wifi_ap_prestart_release(9,42)==ESP_ERR_INVALID_STATE);
 requested.ap.ssid[0]='x';assert(s_ap_prestart->requested.ap.ssid[0]=='n');
 assert(!esp32_mquickjs_wifi_ap_prestart_release(9,41));
 assert(!native_allocations&&!setters&&!mode_creates&&!s_ap_prestart);
 assert(esp32_mquickjs_wifi_ap_prestart_release(9,41)==ESP_ERR_INVALID_STATE);
}
''')

    def test_config_and_readback_precede_ap_start_and_keep_station_mode(self):
        compile_run(self, source() + r'''
int main(void){
 prepare();esp32_mquickjs_wifi_ap_prestart_result_t result;
 assert(esp32_mquickjs_wifi_ap_prestart_activate(9,42,&result)==ESP_ERR_INVALID_STATE&&!setters);
 assert(!esp32_mquickjs_wifi_ap_prestart_activate(9,41,&result));
 assert(result.entered&&result.mutated&&result.verified&&result.mode_ready&&result.returned&&!result.error);
 assert(!result.rollback_attempted&&mode_creates==1&&ap_starts==1&&setters==1&&reads==1);
 assert(stored.ap.ssid[0]=='n'&&mode==WIFI_MODE_APSTA);
 assert(esp32_mquickjs_wifi_ap_prestart_activate(9,41,&result)==ESP_ERR_INVALID_STATE&&setters==1);
 assert(!esp32_mquickjs_wifi_ap_prestart_release(9,41)&&!native_allocations);
}
''')

    def test_native_failure_and_readback_mismatch_preserve_original_error_and_rollback(self):
        compile_run(self, source() + r'''
int main(void){
 for(int failure=1;failure<=3;failure++){
  prepare();fail_set=failure==1?1:failure==3?3:0;bad_read=failure==2;
  esp32_mquickjs_wifi_ap_prestart_result_t result;
  int error=esp32_mquickjs_wifi_ap_prestart_activate(9,41,&result);
  assert(error==(failure==2?ESP_ERR_INVALID_RESPONSE:-77));
  assert(result.error==error&&result.entered&&result.returned&&result.rollback_attempted);
  assert(!result.verified&&!result.mode_ready&&result.ap_quiesced&&!ap_starts&&mode==WIFI_MODE_STA&&setters==2);
  assert(result.rollback_complete==(failure!=3));
  assert(result.rollback_error==(failure==3?-88:0));
  assert(!esp32_mquickjs_wifi_ap_prestart_release(9,41)&&!native_allocations);
 }
}
''')

    def test_disabled_pmf_is_ap_only_before_allocation_and_failure_rolls_back(self):
        compile_run(self, source() + r'''
int main(void){
 for(int failure=0;failure<2;failure++){
  prepare();s_ap_prestart->requested.ap.pmf_cfg.capable=false;
  fail_pmf=failure;esp32_mquickjs_wifi_ap_prestart_result_t result;
  int error=esp32_mquickjs_wifi_ap_prestart_activate(9,41,&result);
  assert(pmf_calls==1&&error==(failure?-92:0));
  if(failure)assert(!ap_starts&&result.ap_quiesced&&result.rollback_complete&&stored.ap.pmf_cfg.capable);
  else assert(ap_starts==1&&!stored.ap.pmf_cfg.capable&&!stored.ap.pmf_cfg.required);
  assert(!esp32_mquickjs_wifi_ap_prestart_release(9,41));
 }
 wifi_config_t requested={0},previous={0};requested.ap.authmode=WIFI_AUTH_WPA3_PSK;
 assert(esp32_mquickjs_wifi_ap_prestart_prepare(9,41,&requested,&previous,matches)==ESP_ERR_NOT_SUPPORTED);
 assert(!native_allocations);
}
''')

    def test_existing_native_ap_or_factory_failure_never_installs_after_allocation(self):
        compile_run(self, source() + r'''
int main(void){
 prepare();g_ic[20]=1;esp32_mquickjs_wifi_ap_prestart_result_t result;
 assert(esp32_mquickjs_wifi_ap_prestart_activate(9,41,&result)==ESP_ERR_INVALID_STATE);
 assert(!setters&&!mode_creates&&!ap_starts);
 assert(!esp32_mquickjs_wifi_ap_prestart_release(9,41));
 prepare();fail_create=true;
 assert(esp32_mquickjs_wifi_ap_prestart_activate(9,41,&result)==-93);
 assert(result.verified&&!result.mode_ready&&result.ap_quiesced&&result.rollback_complete);
 assert(setters==2&&!ap_starts&&stored.ap.ssid[0]=='o');
 assert(!esp32_mquickjs_wifi_ap_prestart_release(9,41));
}
''')

    def test_context_and_dispatch_failure_do_not_write_and_unarmed_mode_is_forwarded(self):
        compile_run(self, source() + r'''
int main(void){
 assert(!__wrap_wifi_mode_set(WIFI_MODE_STA)&&!setters);
 prepare();dispatch_failure=true;esp32_mquickjs_wifi_ap_prestart_result_t result;
 assert(esp32_mquickjs_wifi_ap_prestart_activate(9,41,&result)==-91);
 assert(result.returned&&!result.entered&&!result.mutated&&!setters&&!ap_starts);
 assert(!esp32_mquickjs_wifi_ap_prestart_release(9,41));
 prepare();wrong_task=true;
 assert(esp32_mquickjs_wifi_ap_prestart_activate(9,41,&result)==ESP_ERR_INVALID_STATE);
 assert(!result.mutated&&!setters&&!ap_starts);
 assert(!esp32_mquickjs_wifi_ap_prestart_release(9,41)&&!native_allocations);
}
''')


PREFIX = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#define ESP32_MQUICKJS_WIFI_AP_PRESTART_AVAILABLE 1
#define CONFIG_ESP_WIFI_SOFTAP_SUPPORT 1
#define ESP_OK 0
#define ESP_ERR_INVALID_ARG 0x102
#define ESP_ERR_INVALID_STATE 0x103
#define ESP_ERR_NO_MEM 0x101
#define ESP_ERR_INVALID_RESPONSE 0x108
#define ESP_ERR_NOT_SUPPORTED 0x106
typedef int esp_err_t;
typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
static bool critical;
#define portENTER_CRITICAL(p) do{(void)(p);assert(!critical);critical=true;}while(0)
#define portEXIT_CRITICAL(p) do{(void)(p);assert(critical);critical=false;}while(0)
#define ESP32_MQUICKJS_MEMORY_DEFAULT 0
#define ESP32_MQUICKJS_MEMORY_BUDGET_CONTROL 0
static bool fail_allocation;
static unsigned native_allocations;
static size_t allocation_bytes;
static void*esp32_mquickjs_memory_wireless_calloc(const char*owner,size_t n,size_t bytes,int policy,int role){
 (void)owner;(void)policy;(void)role;assert(!critical&&n==1);
 if(fail_allocation)return NULL;assert(!native_allocations);native_allocations=1;allocation_bytes=bytes;return calloc(n,bytes);
}
static void esp32_mquickjs_memory_payload_free(void*p){
 assert(!critical&&native_allocations==1);
 for(size_t i=0;i<allocation_bytes;i++)assert(!((uint8_t*)p)[i]);
 native_allocations=0;free(p);
}
static void esp32_mquickjs_wireless_secure_zero(void*p,size_t n){memset(p,0,n);}
'''

BOUNDARIES = r'''
static wifi_config_t stored;
static wifi_mode_t mode=WIFI_MODE_STA;
uint8_t g_ic[24];
static int mode_creates,ap_starts,setters,reads,fail_set,pmf_calls;
static bool wifi_task,bad_read,wrong_task,dispatch_failure,fail_pmf,fail_create;
static int esp_wifi_get_mode(wifi_mode_t*out){assert(!critical);*out=mode;return 0;}
static int esp_wifi_get_config(wifi_interface_t interface,wifi_config_t*out){
 assert(!critical&&wifi_task&&interface==WIFI_IF_AP);++reads;*out=stored;
 if(bad_read){out->ap.ssid[0]='?';bad_read=false;}return 0;
}
static int esp_wifi_set_mode(wifi_mode_t mode);
static int esp_wifi_disable_pmf_config(wifi_interface_t interface){
 assert(!critical&&wifi_task&&interface==WIFI_IF_AP&&!g_ic[20]&&!mode_creates&&!ap_starts);
 ++pmf_calls;if(fail_pmf)return -92;stored.ap.pmf_cfg.capable=stored.ap.pmf_cfg.required=false;return 0;
}
static bool matches(const wifi_config_t*a,const wifi_config_t*b){return !memcmp(a,b,sizeof(*a));}
'''

SDK = r'''
bool current_task_is_wifi_task(void){return wifi_task&&!wrong_task;}
int __real_wifi_mode_set(int requested){
 assert(!critical);if(requested==WIFI_MODE_APSTA){
  assert(wifi_task&&mode==WIFI_MODE_STA&&setters==1&&reads==1&&!g_ic[20]);
  if(fail_create)return -93;++mode_creates;g_ic[20]=1;
 }else if(requested==WIFI_MODE_STA)g_ic[20]=0;
 return 0;
}
int esp32qjs_wifi_ap_prestart_set_config_native(wifi_config_t*config){
 assert(!critical&&wifi_task&&mode==WIFI_MODE_STA&&!g_ic[20]&&!mode_creates&&!ap_starts);++setters;
 assert(esp32_mquickjs_wifi_ap_prestart_release(9,41)==ESP_ERR_INVALID_STATE);
 stored=*config;stored.ap.pmf_cfg.capable=true;config->ap.ssid[0]='?';
 if(fail_set==3)return setters==1?-77:-88;
 return fail_set==setters?-77:0;
}
static int esp_wifi_set_mode(wifi_mode_t requested){
 assert(requested==WIFI_MODE_APSTA);if(dispatch_failure)return -91;
 wifi_task=true;int error=__wrap_wifi_mode_set(requested);
 if(!error){assert(stored.ap.ssid[0]=='n'&&reads==1);++ap_starts;mode=requested;}
 else {assert(!__wrap_wifi_mode_set(WIFI_MODE_STA));mode=WIFI_MODE_STA;}
 wifi_task=false;return error;
}
static void prepare(void){
 mode_creates=ap_starts=setters=reads=fail_set=pmf_calls=0;
 wifi_task=bad_read=wrong_task=dispatch_failure=fail_pmf=fail_create=false;mode=WIFI_MODE_STA;memset(g_ic,0,sizeof(g_ic));
 wifi_config_t requested={0},previous={0};requested.ap.ssid[0]='n';previous.ap.ssid[0]='o';
 requested.ap.pmf_cfg.capable=previous.ap.pmf_cfg.capable=true;
 stored=previous;
 assert(!esp32_mquickjs_wifi_ap_prestart_prepare(9,41,&requested,&previous,matches));
}
'''
