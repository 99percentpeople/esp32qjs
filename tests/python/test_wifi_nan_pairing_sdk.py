"""Deferred production PASN command/allocation/timer regressions; no RF claim."""
import importlib.util
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
MODULE = ROOT / 'components/esp32_mquickjs'


def without_includes(source):
    return re.sub(r'^\s*#(?:include[^\n]*|pragma once)\n', '', source, flags=re.M)


def extract(source, name):
    match = re.search(r'^[A-Za-z_][\w *]+\b' + re.escape(name) + r'\([^;{}]*\)\n\{', source, re.M)
    if not match:
        raise AssertionError(name)
    return source[match.start():source.index('\n}\n', match.end()) + 3]


class NanPairingNative(unittest.TestCase):
    def test_checked_production_init_timer_and_cleanup(self):
        compiler = shutil.which('cc')
        if not compiler:
            self.skipTest('Host C compiler unavailable')
        sdk = Path(os.environ.get('IDF_PATH', '/home/zach/esp/esp-idf')) / 'components'
        if not sdk.is_dir():
            self.skipTest('Pinned SDK unavailable')
        spec = importlib.util.spec_from_file_location('pairing_patch', ROOT / 'scripts/patch_idf_nan_pairing.py')
        patch = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(patch)
        with tempfile.TemporaryDirectory() as folder:
            output = Path(folder)
            prepared = patch.prepare(sdk, output / 'prepared')
            native = prepared['esp_nan_supplicant.c']
            code = PREFIX
            code += without_includes((MODULE / 'internal/esp32_mquickjs_wifi_nan_pasn_sdk.h').read_text())
            code += without_includes(prepared['esp_nan_supp_i.h'])
            code += BOUNDARY
            code += without_includes((MODULE / 'src/modules/wifi_nan/esp32_mquickjs_wifi_nan_pasn_state.inc').read_text())
            for name in ('nan_pasn_auth_timeout_cb', 'nan_pasn_auth_timeout_cancel', 'nan_pasn_auth_timeout_arm',
                         'nan_set_dev_sae_pin', 'nan_pasn_data_deinit', 'nan_pasn_data_init', 'nan_pasn_auth_initiate'):
                code += extract(native, name)
            code += without_includes((MODULE / 'src/modules/wifi_nan/esp32_mquickjs_wifi_nan_pasn_commands.inc').read_text())
            code += MAIN
            source, binary = output / 'case.c', output / 'case'
            source.write_text(code)
            result = subprocess.run([compiler, '-std=c11', '-Wall', '-Wextra', '-Werror', str(source), '-o', str(binary)],
                capture_output=True, text=True, timeout=60)
            self.assertEqual(result.returncode, 0, result.stderr)
            result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=20)
            self.assertEqual(result.returncode, 0, result.stderr)


PREFIX = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#define CONFIG_ESP32_MQUICKJS_FEATURE_WIFI 1
#define CONFIG_ESP_WIFI_NAN_PAIRING 1
#define ETH_ALEN 6
#define NAN_PASN_AUTH_TIMEOUT_SECS 10
#define ELOOP_ALL_CTX ((void*)-1)
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_NO_MEM 101
#define ESP_ERR_INVALID_ARG 102
#define ESP_ERR_INVALID_STATE 103
#define ESP_ERR_TIMEOUT 104
#define ESP32_MQUICKJS_MEMORY_DEFAULT 0
#define ESP32_MQUICKJS_MEMORY_BUDGET_CONTROL 1
#define WIFI_IF_NAN 2
#define MSG_INFO 1
#define os_snprintf snprintf
#define os_memset memset
#define os_strlen strlen
#define os_snprintf_error(size,n) ((n)<0||(size_t)(n)>=(size))
typedef int esp_err_t;
enum nan_role {NAN_ROLE_IDLE,NAN_ROLE_PAIRING_INITIATOR,NAN_ROLE_PAIRING_RESPONDER};
typedef void(*esp_nan_pairing_key_installed_cb_t)(void);
struct pasn_data {int unused;};
struct rsn_pmksa_cache {int unused;};
static bool wifi_task;
static unsigned allocations,allocation_calls,fail_allocation,cache_alive,cache_calls,fail_cache,dispatches;
static int mac_error,pending_error,dispatch_error,protocol_error;
static bool tx_busy,unbound;
static void (*timer_cb)(void*,void*);
static void *timer_data,*timer_arg;
static unsigned timer_calls;
static bool fail_timer;
static int pasn_nd_pmk_global;
static void forced_memzero(void*p,size_t n){volatile uint8_t*b=p;while(n--)*b++=0;}
static void wpa_printf(int level,const char*fmt,...){(void)level;(void)fmt;}
bool current_task_is_wifi_task(void){return wifi_task;}
static bool is_zero_ether_addr(const uint8_t*p){return !memcmp(p,(uint8_t[6]){0},6);}
static void callback(void){}
static esp_nan_pairing_key_installed_cb_t esp_nan_pairing_get_key_installed_cb(void){return callback;}
'''

BOUNDARY = r'''
static struct nan_pasn_data *active;
static struct nan_pasn_data *esp_nan_app_get_pasn_data(void){return active;}
static void esp_nan_app_set_pasn_data(struct nan_pasn_data*p){active=p;}
static void *esp32_mquickjs_memory_wireless_calloc(const char*owner,size_t n,size_t z,int cls,int role){
 assert(wifi_task&&!strcmp(owner,"wifi.nan")&&n==1&&!cls&&role==1);
 if(++allocation_calls==fail_allocation)return NULL;
 size_t*p=calloc(1,sizeof(size_t)+z);assert(p);*p=z;++allocations;return p+1;
}
static void esp32_mquickjs_memory_payload_free(void*ptr){
 assert(ptr&&allocations);size_t*p=(size_t*)ptr-1;
 for(size_t i=0;i<*p;i++){assert(!((uint8_t*)ptr)[i]);}
 --allocations;free(p);
}
static struct rsn_pmksa_cache *new_cache(void){
 if(++cache_calls==fail_cache)return NULL;
 ++cache_alive;return calloc(1,sizeof(struct rsn_pmksa_cache));
}
static struct rsn_pmksa_cache *pasn_initiator_pmksa_cache_init(void){return new_cache();}
static struct rsn_pmksa_cache *pasn_responder_pmksa_cache_init(void){return new_cache();}
static void pasn_initiator_pmksa_cache_deinit(struct rsn_pmksa_cache*p){assert(cache_alive);--cache_alive;free(p);}
static void pasn_responder_pmksa_cache_deinit(struct rsn_pmksa_cache*p){assert(cache_alive);--cache_alive;free(p);}
static void wpa_pasn_reset(struct pasn_data*p){(void)p;}
static void pasn_data_deinit(struct pasn_data*p){free(p);}
static int esp_wifi_get_mac(int interface,uint8_t*out){assert(interface==WIFI_IF_NAN);memset(out,2,6);return mac_error;}
static int nan_pasn_esp_send_mgmt(void*c,const uint8_t*d,size_t n,int a,unsigned f,unsigned w){
 (void)c;(void)d;(void)n;(void)a;(void)f;(void)w;return 0;
}
static int eloop_register_timeout(unsigned s,unsigned u,void(*cb)(void*,void*),void*d,void*a){
 (void)s;(void)u;++timer_calls;if(fail_timer)return -1;timer_cb=cb;timer_data=d;timer_arg=a;return 0;
}
static int eloop_cancel_timeout(void(*cb)(void*,void*),void*d,void*a){
 if(timer_cb!=cb||timer_data!=d||(a!=ELOOP_ALL_CTX&&a!=timer_arg))return 0;
 timer_cb=NULL;timer_data=timer_arg=NULL;return 1;
}
static int eloop_register_timeout_blocking(int(*fn)(void*,void*),void*a,void*b){
 assert(!wifi_task);++dispatches;if(dispatch_error)return dispatch_error;
 wifi_task=true;int result=fn(a,b);wifi_task=false;return result;
}
static void nan_pasn_clear_saved_keys(void){}
static int64_t esp_timer_get_time(void){return 1234;}
bool esp32_mquickjs_wifi_nan_pairing_followup_pending(void){return pending_error!=0;}
bool esp32_mquickjs_wifi_nan_tx_pairing_settled(uint32_t id,esp_err_t*error,int64_t*completed){
 (void)id;*error=0;*completed=1234;return !tx_busy;
}
esp_err_t esp32_mquickjs_wifi_nan_pairing_cancel_pending(void){assert(wifi_task);return pending_error;}
bool esp32_mquickjs_wifi_nan_pairing_services(const uint8_t p[6],uint8_t*own,uint8_t*remote){
 assert(wifi_task&&p);if(own)*own=1;if(remote)*remote=2;return !unbound;
}
void esp32_mquickjs_wifi_nan_pairing_release_binding(void){assert(wifi_task);}
bool esp32_mquickjs_wifi_nan_tx_pairing_drained(uint32_t id){(void)id;assert(wifi_task);return !tx_busy;}
static int nan_pasn_initialize(struct nan_pasn_data*p,const uint8_t*peer){
 memcpy(p->pasn_unicast_peer,peer,6);p->pasn=calloc(1,sizeof(struct pasn_data));return protocol_error;
}
static int nan_prepare_pasn_extra_ie(struct nan_pasn_data*p,struct pasn_data*q,void*r,bool verify){
 (void)p;(void)q;(void)r;(void)verify;return protocol_error;
}
int nan_initiate_pasn_auth(struct nan_pasn_data*p,const uint8_t*peer){return nan_pasn_initialize(p,peer);}
int nan_initiate_pasn_verify(struct nan_pasn_data*p,const uint8_t*peer,int r,const uint8_t*b,const uint8_t*s,size_t l){
 (void)r;(void)b;(void)s;(void)l;return nan_pasn_initialize(p,peer);
}
static void nan_pasn_auth_timeout_cancel(struct nan_pasn_data*);
'''

MAIN = r'''
int main(void){
 uint8_t peer[6]={2,1,2,3,4,5};
 esp32_mquickjs_wifi_nan_pasn_status_t status;
 assert(esp_nan_supp_pasn_responder_init(peer,UINT32_MAX,0,callback)==-1);
 assert(!allocation_calls&&!dispatches&&!active);
 unbound=true;
 assert(esp_nan_supp_pasn_responder_init(peer,123456,0,callback)==-1&&!allocation_calls);
 unbound=false;
 for(unsigned n=1;n<=2;n++){
  fail_allocation=allocation_calls+n;
  assert(esp_nan_supp_pasn_responder_init(peer,123456,0,callback)==-1);
  assert(!active&&!allocations&&!cache_alive);
  fail_allocation=0;
 }
 for(unsigned n=1;n<=2;n++){
  fail_cache=cache_calls+n;
  assert(esp_nan_supp_pasn_responder_init(peer,123456,0,callback)==-1);
  assert(!active&&!allocations&&!cache_alive);fail_cache=0;
 }
 mac_error=-77;
 assert(esp_nan_supp_pasn_responder_init(peer,123456,0,callback)==-1);
 assert(!allocations&&!cache_alive);mac_error=0;
 assert(!esp_nan_supp_pasn_responder_init(peer,123456,0,callback));
 assert(active&&!strcmp(active->dev_sae_pin,"123456"));
 assert(!esp32_mquickjs_wifi_nan_pasn_status(&status)&&status.active);
 uint32_t old_identity=status.identity;struct nan_pasn_data*old=active;
 void(*old_timer)(void*,void*)=timer_cb;void*old_data=timer_data;void*old_arg=timer_arg;
 assert(esp_nan_supp_pasn_responder_init(peer,654321,0,callback)==-1);
 assert(active==old&&!strcmp(active->dev_sae_pin,"123456"));
 assert(esp32_mquickjs_wifi_nan_pasn_close(old_identity+1)==ESP_ERR_INVALID_STATE&&active==old);
 pending_error=ESP_ERR_TIMEOUT;
 assert(esp32_mquickjs_wifi_nan_pasn_close(old_identity)==ESP_ERR_TIMEOUT&&active==old&&allocations);
 pending_error=0;tx_busy=true;
 assert(esp32_mquickjs_wifi_nan_pasn_close(old_identity)==ESP_ERR_TIMEOUT&&active==old&&allocations);
 tx_busy=false;assert(!esp32_mquickjs_wifi_nan_pasn_close(old_identity));
 assert(!active&&!allocations&&!cache_alive&&!timer_cb);
 assert(!esp_nan_supp_pasn_responder_init(peer,111222,0,callback));
 assert(active->framework_identity>old_identity);old=active;
 wifi_task=true;old_timer(old_data,old_arg);wifi_task=false;
 assert(active==old&&!strcmp(active->dev_sae_pin,"111222"));
 wifi_task=true;timer_cb(timer_data,timer_arg);wifi_task=false;
 assert(!active&&!allocations&&!cache_alive);
 assert(!esp32_mquickjs_wifi_nan_pasn_status(&status)&&status.error==ESP_ERR_TIMEOUT&&status.retired);
 assert(!esp32_mquickjs_wifi_nan_pasn_shutdown());
 protocol_error=-88;
 assert(esp_nan_supp_pasn_initiator_auth(peer,123456,0,callback)==-1);
 assert(!active&&!allocations&&!cache_alive);
 assert(!esp32_mquickjs_wifi_nan_pasn_status(&status)&&status.error&&status.retired);
 protocol_error=0;
 unsigned rejected_calls=allocation_calls;
 assert(esp_nan_supp_pasn_responder_init(peer,123456,0,callback)==-1&&allocation_calls==rejected_calls);
 assert(!esp32_mquickjs_wifi_nan_pasn_shutdown());
 fail_timer=true;
 assert(esp_nan_supp_pasn_responder_init(peer,123456,0,callback)==-1);
 assert(!active&&!allocations&&!cache_alive);fail_timer=false;
 assert(!esp32_mquickjs_wifi_nan_pasn_shutdown());
 dispatch_error=ESP_ERR_TIMEOUT;rejected_calls=allocation_calls;
 assert(esp_nan_supp_pasn_responder_init(peer,123456,0,callback)==-1&&allocation_calls==rejected_calls);
 assert(!active&&!allocations);dispatch_error=0;
 assert(!esp_nan_supp_pasn_responder_init(peer,123456,0,callback));
 tx_busy=true;
 assert(!esp32_mquickjs_wifi_nan_pasn_shutdown()&&!active&&!allocations);
 tx_busy=false; /* Parent Radio still owns the independently tracked EBs. */
 s_esp32qjs_pasn_last_identity=UINT32_MAX;
 unsigned before=allocation_calls;
 assert(esp_nan_supp_pasn_initiator_auth(peer,123456,0,callback)==-1);
 assert(allocation_calls==before&&!active&&!allocations);
}
'''
