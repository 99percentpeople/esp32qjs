"""Deferred tests of the actual SDK-owned WAPI lifecycle wrapper.

Native WAPI calls and the critical-section boundary are injected; no separate
lifecycle model replaces the production implementation.
"""
import re
import unittest
from pathlib import Path
from test_wireless_control_regression import compile_run
from wireless_vm_fixture import extract

BASE = Path(__file__).resolve().parents[2] / 'components/esp32_mquickjs'


def source():
    clean = lambda text: re.sub(r'^\s*#(?:include[^\n]*|pragma once)\n', '', text, flags=re.M)
    return PREFIX + clean((BASE / 'internal/esp32_mquickjs_wifi_wapi.h').read_text()) + \
        clean((BASE / 'src/modules/wifi/esp32_mquickjs_wifi_wapi_sdk.c').read_text()) + NATIVE


class WiFiWapiSdk(unittest.TestCase):
    def test_radio_preflight_never_uses_observation_as_mutation_authority(self):
        radio = (BASE / 'src/modules/wifi_radio/esp32_mquickjs_wifi_radio.c').read_text()
        code = source() + RADIO_BOUNDARY + extract(radio, 'esp32_mquickjs_wifi_radio_wapi_prepare')
        code += extract(radio, 'esp32_mquickjs_wifi_radio_wapi_select')
        compile_run(self, code + r'''
int main(void){
 bool rebuild=true;assert(!esp32_mquickjs_wifi_radio_wapi_prepare(false,&rebuild)&&!rebuild);
 s_radio.leases[1].identity=55;
 assert(esp32_mquickjs_wifi_radio_wapi_prepare(true,&rebuild)==ESP_ERR_INVALID_STATE&&!rebuild);
 assert(!s_wapi.requested_enabled);s_radio.leases[1].identity=0;
 assert(!__wrap_esp_wifi_internal_wapi_init());s_radio.driver_owned=true;s_radio.driver_state=2;snapshot_unchanged=true;
 assert(!esp32_mquickjs_wifi_radio_wapi_prepare(true,&rebuild)&&rebuild&&!s_wapi.requested_enabled);
 esp32_mquickjs_wifi_radio_lifecycle_t token={.identity=41};
 assert(esp32_mquickjs_wifi_radio_wapi_select(&token,1,true)==ESP_ERR_INVALID_STATE&&!s_wapi.requested_enabled);
 checkpoint_exact=true;stopped_fenced=false;
 assert(esp32_mquickjs_wifi_radio_wapi_select(&token,1,true)==ESP_ERR_INVALID_STATE&&!s_wapi.requested_enabled);
 stopped_fenced=true;assert(!esp32_mquickjs_wifi_radio_wapi_select(&token,1,true)&&s_wapi.requested_enabled&&!inits);
 assert(!__wrap_esp_wifi_internal_wapi_deinit()&&!deinits);
 assert(!__wrap_esp_wifi_internal_wapi_init()&&inits==1);assert(!__wrap_esp_wifi_internal_wapi_deinit());
 assert(!radio_locked&&!lock_depth);
}
''')

    def test_disabled_init_skip_duplicate_guard_and_physical_generation(self):
        compile_run(self, source() + r'''
int main(void){
 assert(!esp32_mquickjs_wifi_wapi_sdk_policy(false));
 assert(!__wrap_esp_wifi_internal_wapi_init()&&!inits);
 esp32_mquickjs_wifi_wapi_status_t s;esp32_mquickjs_wifi_wapi_sdk_status(&s);
 assert(s.supplicant_active&&!s.enabled&&!s.requested_enabled&&s.revision==2&&s.generation==1);
 assert(__wrap_esp_wifi_internal_wapi_init()==ESP_ERR_INVALID_STATE&&!inits);
 assert(!__wrap_esp_wifi_internal_wapi_deinit()&&!deinits);
 assert(!esp32_mquickjs_wifi_wapi_sdk_cleanup_error());
 assert(!esp32_mquickjs_wifi_wapi_sdk_policy(true));
 assert(!__wrap_esp_wifi_internal_wapi_init()&&inits==1);
 assert(__wrap_esp_wifi_internal_wapi_init()==ESP_ERR_INVALID_STATE&&inits==1);
 assert(!__wrap_esp_wifi_internal_wapi_deinit()&&deinits==1);
 assert(!__wrap_esp_wifi_internal_wapi_deinit()&&deinits==1);
 esp32_mquickjs_wifi_wapi_sdk_status(&s);assert(s.generation==2&&!s.enabled&&!s.busy&&!s.uncertain);
}
''')

    def test_reviewed_partial_init_errors_do_not_call_deinit_on_unpublished_state(self):
        compile_run(self, source() + r'''
int main(void){
 for(int failure=-1;failure>=-3;--failure){
  native_init_error=failure;assert(__wrap_esp_wifi_internal_wapi_init()==failure);
  esp32_mquickjs_wifi_wapi_status_t s;esp32_mquickjs_wifi_wapi_sdk_status(&s);
  assert(s.error==failure&&!s.enabled&&!s.supplicant_active&&!s.uncertain);
  assert(!__wrap_esp_wifi_internal_wapi_deinit()&&!deinits);
 }
 assert(inits==3&&!lock_depth);
}
''')

    def test_uncertain_deinit_retains_raw_error_and_never_replays_native_free(self):
        compile_run(self, source() + r'''
int main(void){
 assert(!__wrap_esp_wifi_internal_wapi_init());native_deinit_error=-74;
 assert(__wrap_esp_wifi_internal_wapi_deinit()==-74);
 assert(esp32_mquickjs_wifi_wapi_sdk_cleanup_error()==-74);
 assert(__wrap_esp_wifi_internal_wapi_deinit()==ESP_ERR_INVALID_STATE&&deinits==1);
 assert(__wrap_esp_wifi_internal_wapi_init()==ESP_ERR_INVALID_STATE&&inits==1);
 assert(esp32_mquickjs_wifi_wapi_sdk_policy(false)==ESP_ERR_INVALID_STATE);
 esp32_mquickjs_wifi_wapi_status_t s;esp32_mquickjs_wifi_wapi_sdk_status(&s);
 assert(s.uncertain&&s.cleanup_error==-74&&!s.busy);
}
''')

    def test_busy_reentrancy_and_identity_exhaustion_do_not_reuse_native_state(self):
        compile_run(self, source() + r'''
int main(void){
 reenter=true;assert(!__wrap_esp_wifi_internal_wapi_init()&&inits==1);
 assert(!__wrap_esp_wifi_internal_wapi_deinit());
 s_wapi.revision=UINT32_MAX;assert(esp32_mquickjs_wifi_wapi_sdk_policy(false)==ESP_ERR_NO_MEM);
 assert(s_wapi.revision==UINT32_MAX&&s_wapi.requested_enabled);
 s_wapi.generation=UINT32_MAX;assert(__wrap_esp_wifi_internal_wapi_init()==ESP_ERR_NO_MEM&&inits==1);
 assert(s_wapi.generation==UINT32_MAX);
}
''')


PREFIX = r'''
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <limits.h>
#define CONFIG_ESP32_MQUICKJS_FEATURE_WIFI 1
#define CONFIG_ESP_WIFI_WAPI_PSK 1
#define ESP_OK 0
#define ESP_ERR_INVALID_STATE 259
#define ESP_ERR_NO_MEM 257
typedef int esp_err_t,JSContext,JSValue,portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
static int lock_depth;
#define portENTER_CRITICAL(p) do{(void)(p);assert(!lock_depth);++lock_depth;}while(0)
#define portEXIT_CRITICAL(p) do{(void)(p);assert(lock_depth==1);--lock_depth;}while(0)
'''

NATIVE = r'''
static unsigned inits,deinits;static int native_init_error,native_deinit_error;static bool reenter;
esp_err_t __real_esp_wifi_internal_wapi_init(void){
 assert(!lock_depth);++inits;
 if(reenter){assert(__wrap_esp_wifi_internal_wapi_init()==ESP_ERR_INVALID_STATE);
  assert(__wrap_esp_wifi_internal_wapi_deinit()==ESP_ERR_INVALID_STATE);
  assert(esp32_mquickjs_wifi_wapi_sdk_policy(false)==ESP_ERR_INVALID_STATE);}
 return native_init_error;
}
esp_err_t __real_esp_wifi_internal_wapi_deinit(void){assert(!lock_depth);++deinits;return native_deinit_error;}
'''

RADIO_BOUNDARY = r'''
#define ESP_ERR_INVALID_ARG 258
#define WIFI_RADIO_MAX_LEASES 2
#define ESP32_MQUICKJS_WIFI_RADIO_UNINITIALIZED 0
typedef int wifi_mode_t;
typedef struct{uint32_t identity,generation;} esp32_mquickjs_wifi_radio_lifecycle_t;
static struct{
 esp32_mquickjs_wifi_radio_lifecycle_t lifecycle,operation,leases[WIFI_RADIO_MAX_LEASES];
 const char*fault_stage,*cleanup_stage;unsigned wake_locks;
 bool restart_required,driver_owned,promiscuous_claimed;int driver_state;
}s_radio;
static bool radio_locked,snapshot_unchanged,checkpoint_exact,stopped_fenced;
static void wifi_radio_operation_lock(void){assert(!radio_locked);radio_locked=true;}
static void wifi_radio_operation_unlock(void){assert(radio_locked);radio_locked=false;}
static bool wifi_radio_stop_snapshot_unchanged_locked(void){assert(radio_locked);return snapshot_unchanged;}
static bool wifi_radio_restart_checkpoint_matches_locked(const esp32_mquickjs_wifi_radio_lifecycle_t*t,wifi_mode_t mode){
 assert(radio_locked&&t->identity==41&&mode==1);return checkpoint_exact;
}
static int wifi_radio_check_stopped_lifecycle_locked(const esp32_mquickjs_wifi_radio_lifecycle_t*t,bool detached){
 assert(radio_locked&&t->identity==41&&detached);return stopped_fenced?ESP_OK:ESP_ERR_INVALID_STATE;
}
'''
