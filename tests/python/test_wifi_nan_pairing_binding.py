"""Deferred tests of production pairing admission and selected NPK lookup.

Native service/cache storage and crypto submission are injected boundaries;
the actual binding, command dispatch and credential-selection helpers run.
These fixtures are authored now, executed with the Wi-Fi stage validation.
"""
import importlib.util
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
BASE = ROOT / 'components/esp32_mquickjs'


def clean(source):
    return re.sub(r'^\s*#(?:include[^\n]*|pragma once)\n', '', source, flags=re.M)


def extract(source, name):
    match = re.search(r'^[A-Za-z_][\w *]+\b' + re.escape(name) + r'\([^;{}]*\)\n\{', source, re.M)
    if not match:
        raise AssertionError(name)
    return source[match.start():source.index('\n}\n', match.end()) + 3]


class PairingBinding(unittest.TestCase):
    def test_exact_service_explicit_credential_failure_and_scrub(self):
        compiler = shutil.which('cc')
        sdk = Path(os.environ.get('IDF_PATH', '/home/zach/esp/esp-idf')) / 'components'
        if not compiler or not sdk.is_dir():
            self.skipTest('Host compiler and pinned SDK required')
        spec = importlib.util.spec_from_file_location('nan_pairing_binding_patch', ROOT / 'scripts/patch_idf_nan_pairing.py')
        patch = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(patch)
        with tempfile.TemporaryDirectory() as directory:
            folder = Path(directory)
            native = patch.prepare(sdk, folder / 'prepared')['nan_pairing.c']
            spec = importlib.util.spec_from_file_location('nan_cache_patch', ROOT / 'scripts/patch_idf_nan.py')
            cache_patch = importlib.util.module_from_spec(spec)
            spec.loader.exec_module(cache_patch)
            app = cache_patch.patch_source((sdk / 'esp_wifi/wifi_apps/nan_app/src/nan_app.c').read_text())
            code = PREFIX + clean((BASE / 'internal/esp32_mquickjs_wifi_nan_pasn_sdk.h').read_text()) + BOUNDARY
            code += clean((BASE / 'src/modules/wifi_nan/esp32_mquickjs_wifi_nan_pairing_binding.inc').read_text())
            code += extract(native, 'nan_peer_cred_npk_present')
            code += extract(native, 'nan_peer_cred_lookup')
            code += extract(native, 'nan_global_peer_npk_lookup')
            sdk_bridge = (BASE / 'src/modules/wifi_nan/esp32_mquickjs_wifi_nan_sdk.inc').read_text()
            for name in ('esp32qjs_nan_pairing_reset_service', 'esp32_mquickjs_wifi_nan_pairing_cached_peer'):
                code += extract(sdk_bridge, name)
            for name in ('nan_app_find_paired_slot_locked', 'nan_app_alloc_paired_slot_locked', 'nan_app_register_paired_peer',
                         'nan_app_remove_paired_peer'):
                code += extract(app, name)
            code += NATIVE
            code += clean((BASE / 'src/modules/wifi_nan/esp32_mquickjs_wifi_nan_pairing_start.inc').read_text())
            code += extract(native, 'nan_app_update_peer_creds')
            code += clean((BASE / 'src/modules/wifi_nan/esp32_mquickjs_wifi_nan_pairing_cache.inc').read_text()) + MAIN
            source, binary = folder / 'case.c', folder / 'case'
            source.write_text(code)
            result = subprocess.run([compiler, '-std=c11', '-Wall', '-Wextra', '-Werror', str(source), '-o', str(binary)],
                capture_output=True, text=True, timeout=60)
            self.assertEqual(result.returncode, 0, result.stderr)
            result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=15)
            self.assertEqual(result.returncode, 0, result.stderr)


PREFIX = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#define CONFIG_ESP32_MQUICKJS_FEATURE_WIFI 1
#define CONFIG_ESP_WIFI_NAN_PAIRING 1
#define ESP_WIFI_NAN_NPK_LEN 32
#define ESP_WIFI_NAN_NIK_LEN 16
#define ESP_WIFI_NAN_NDP_PMK_LEN 32
#define ESP_WIFI_NAN_DATAPATH_MAX_PEERS 2
#define ESP_WIFI_NAN_MAX_PEER_CREDS 2
#define WIFI_NAN_CSID_NCS_SK_128 1
#define WIFI_NAN_CSID_NCS_SK_256 2
#define MACADDR_LEN 6
#define MACADDR_COPY(a,b) memcpy(a,b,6)
#define ESP_LOGI(...) ((void)0)
#define NAN_APP_PEER_NIK_LEN 16
#define ESP_OK 0
#define ESP_ERR_INVALID_ARG 1
#define ESP_ERR_INVALID_STATE 2
#define ESP_ERR_NO_MEM 3
#define ESP_ERR_TIMEOUT 4
#define ESP32_MQUICKJS_MEMORY_DEFAULT 0
#define ESP32_MQUICKJS_MEMORY_BUDGET_CONTROL 1
#define WPA_KEY_MGMT_SAE 7
#define NAN_PAIRING_PINCODE_MAX 999999
#define NAN_PAIRING_ROLE_INITIATOR 0
#define NAN_PAIRING_ROLE_RESPONDER 1
#define MACADDR_EQUAL(a,b) (!memcmp(a,b,6))
typedef int esp_err_t;
typedef struct {uint8_t peer_nik[16],npk[32],service_hash[6];bool is_valid;} wifi_nan_peer_creds_t;
typedef struct {uint8_t peer_svc_id,peer_nmi[6];int self_role;bool pairing_verification;struct {uint32_t pincode;} cred;} wifi_nan_pairing_config_t;
struct own_svc_info {uint8_t svc_id,svc_hash[6],verify_session_peer_nmi[6];bool verify_session_pending;
 struct {bool pairing_setup,pairing_verification,npk_nik_caching;} pairing;};
struct peer_svc_info {bool has_nik;uint8_t peer_nik[16];};
struct nan_paired_peer {bool valid;uint8_t peer_nmi[6],role,ndp_csid,nd_pmk[32];uint32_t lifetime_sec;int64_t established_us;};
void nan_app_remove_paired_peer(const uint8_t *peer);
static struct own_svc_info services[2];
static struct peer_svc_info peer;
static struct {wifi_nan_peer_creds_t peer_creds[2];unsigned num_peer_creds;struct nan_paired_peer paired_peers[2];} s_nan_ctx;
static uint8_t cache_services[2];
static int64_t native_clock=100;
static int64_t esp_timer_get_time(void){return native_clock;}
uint8_t esp32_mquickjs_wifi_nan_pairing_cache_service(unsigned slot){assert(slot<2);return cache_services[slot];}
bool esp32_mquickjs_wifi_nan_pairing_cache_bind(unsigned slot,uint8_t service){assert(slot<2);cache_services[slot]=service;return true;}
static bool s_nan_data_lock=true,wifi_task,locked,fail_alloc,peer_busy,fail_native;
static unsigned dispatches,allocations,native_calls,last_identity;
#define NAN_DATA_LOCK() do {assert(!locked);locked=true;}while(0)
#define NAN_DATA_UNLOCK() do {assert(locked);locked=false;}while(0)
static void forced_memzero(void*p,size_t n){volatile uint8_t*b=p;while(n--)*b++=0;}
bool current_task_is_wifi_task(void){return wifi_task;}
static struct own_svc_info *nan_find_own_svc(uint8_t id){assert(locked);return id>=1&&id<=2?&services[id-1]:NULL;}
static struct peer_svc_info *nan_find_peer_svc_exact(uint8_t own,uint8_t remote,const uint8_t*mac){
 assert(locked);return own>=1&&own<=2&&remote==9&&mac[0]==2?&peer:NULL;
}
static void *esp32_mquickjs_memory_wireless_calloc(const char*owner,size_t n,size_t size,int policy,int role){
 assert(wifi_task&&locked&&!strcmp(owner,"wifi.nan")&&n==1&&policy==0&&role==1);
 if(fail_alloc)return NULL;
 size_t*p=calloc(1,sizeof(size_t)+size);assert(p);*p=size;++allocations;return p+1;
}
static void esp32_mquickjs_memory_payload_free(void*ptr){
 assert(wifi_task&&!locked&&ptr&&allocations);size_t*p=(size_t*)ptr-1;
 for(size_t i=0;i<*p;i++){assert(!((uint8_t*)ptr)[i]);}
 --allocations;free(p);
}
static int eloop_register_timeout_blocking(int(*callback)(void*,void*),void*a,void*b){
 assert(!wifi_task&&!locked);++dispatches;wifi_task=true;int r=callback(a,b);wifi_task=false;return r;
}
static void *esp_nan_app_get_pasn_data(void){return NULL;}
static bool esp32_mquickjs_wifi_nan_ndp_peer_busy(const uint8_t*mac){assert(wifi_task&&mac);return peer_busy;}
'''

BOUNDARY = r'''
static esp32_mquickjs_wifi_nan_credential_info_t credential_meta[2];
static uint32_t next_credential=201;
bool esp32_mquickjs_wifi_nan_credential_info(unsigned slot,esp32_mquickjs_wifi_nan_credential_info_t*out){
 *out=(esp32_mquickjs_wifi_nan_credential_info_t){0};if(slot>=2)return false;*out=credential_meta[slot];
 return out->identity&&(!out->expires_us||native_clock<out->expires_us);
}
esp_err_t esp32_mquickjs_wifi_nan_credential_replace(unsigned slot,const uint8_t mac[6],int64_t expires,uint32_t*out){
 assert(locked&&wifi_task&&slot<2);*out=0;if(!next_credential)return ESP_ERR_NO_MEM;
 credential_meta[slot]=(esp32_mquickjs_wifi_nan_credential_info_t){.identity=next_credential++,.expires_us=expires};
 memcpy(credential_meta[slot].peer,mac,6);*out=credential_meta[slot].identity;return 0;
}

static esp32_mquickjs_wifi_nan_pasn_status_t snapshot;
esp_err_t esp32_mquickjs_wifi_nan_pasn_status(esp32_mquickjs_wifi_nan_pasn_status_t*out){*out=snapshot;return 0;}
uint32_t esp32_mquickjs_wifi_nan_pasn_identity(const uint8_t*mac){
 return snapshot.closing||(mac&&memcmp(mac,snapshot.peer,6))?0:snapshot.identity;
}
'''

NATIVE = r'''
static esp_err_t esp32qjs_pairing_start_native(wifi_nan_pairing_config_t*config){
 assert(wifi_task&&!locked);++native_calls;
 if(fail_native)return ESP_ERR_NO_MEM;
 snapshot=(esp32_mquickjs_wifi_nan_pasn_status_t){.identity=++last_identity,.active=true};
 memcpy(snapshot.peer,config->peer_nmi,6);
 uint8_t own=0,remote=0;
 assert(esp32_mquickjs_wifi_nan_pairing_services(config->peer_nmi,&own,&remote)&&own==2&&remote==9);
 if(config->pairing_verification){uint8_t npk[32];size_t len=0;int akmp=0;
  assert(!nan_global_peer_npk_lookup(npk,&len,&akmp)&&len==32&&akmp==7&&npk[0]==0x62);
 }
 return ESP_OK;
}
static void release(void){wifi_task=true;snapshot.closing=true;esp32_mquickjs_wifi_nan_pairing_release_binding();wifi_task=false;}
'''

MAIN = r'''
int main(void){
 services[0]=(struct own_svc_info){.svc_id=1,.svc_hash={1},.pairing={true,true,true}};
 services[1]=(struct own_svc_info){.svc_id=2,.svc_hash={2},.pairing={true,true,true}};
 credential_meta[0].identity=101;credential_meta[1].identity=102;
 s_nan_ctx.num_peer_creds=2;
 for(unsigned i=0;i<2;i++)s_nan_ctx.peer_creds[i]=(wifi_nan_peer_creds_t){.is_valid=true,.service_hash={i?2:1},.npk={0x61+i},.peer_nik={i+1}};
 esp32_mquickjs_wifi_nan_pairing_config_t config={.service_identity=44,.service_id=2,.peer_service_id=9,
  .peer={2,1,2,3,4,5},.verification=true,.initiator=true,.credential_id=101};
 esp32_mquickjs_wifi_nan_pasn_status_t result;
 assert(esp32_mquickjs_wifi_nan_pairing_start(&config,&result)==ESP_ERR_INVALID_ARG&&!allocations&&!native_calls);
 config.credential_id=104;
 assert(esp32_mquickjs_wifi_nan_pairing_start(&config,&result)==ESP_ERR_INVALID_ARG&&!allocations&&!native_calls);
 config.credential_id=102;peer_busy=true;
 assert(esp32_mquickjs_wifi_nan_pairing_start(&config,&result)==ESP_ERR_INVALID_STATE&&!allocations&&!native_calls);
 peer_busy=false;fail_alloc=true;
 assert(esp32_mquickjs_wifi_nan_pairing_start(&config,&result)==ESP_ERR_NO_MEM&&!allocations&&!native_calls);
 fail_alloc=false;fail_native=true;snapshot.identity=29;snapshot.closing=true;
 assert(esp32_mquickjs_wifi_nan_pairing_start(&config,&result)==ESP_ERR_NO_MEM&&!allocations&&!result.identity);
 fail_native=false;
 assert(!esp32_mquickjs_wifi_nan_pairing_start(&config,&result)&&result.identity&&allocations==1);
 assert(esp32_mquickjs_wifi_nan_pairing_service_idle(2)==ESP_ERR_INVALID_STATE);
 assert(esp32_mquickjs_wifi_nan_pairing_service_idle(1)==ESP_OK);
 s_nan_ctx.peer_creds[1].npk[0]=0xff; /* Mutable cache is not the operation's key. */
 wifi_task=true;uint8_t npk[32];size_t len=0;int akmp=0;
 assert(!nan_global_peer_npk_lookup(npk,&len,&akmp)&&npk[0]==0x62);
 assert(!esp32_mquickjs_wifi_nan_pairing_services((uint8_t[6]){2,9},NULL,NULL));
 uint8_t nd_pmk[32]={0x81};
 assert(!nan_app_register_paired_peer(config.peer,0,1,nd_pmk,sizeof(nd_pmk),1));
 NAN_DATA_LOCK();
 assert(esp32_mquickjs_wifi_nan_pairing_cached_peer(2,config.peer)==&s_nan_ctx.paired_peers[0]);
 assert(!esp32_mquickjs_wifi_nan_pairing_cached_peer(1,config.peer));
 native_clock+=1000000;assert(!esp32_mquickjs_wifi_nan_pairing_cached_peer(2,config.peer));
 s_nan_ctx.paired_peers[1]=s_nan_ctx.paired_peers[0];cache_services[1]=1;
 esp32qjs_nan_pairing_reset_service(2);
 assert(!s_nan_ctx.paired_peers[0].valid&&s_nan_ctx.paired_peers[1].valid&&!cache_services[0]);
 s_nan_ctx.paired_peers[0]=s_nan_ctx.paired_peers[1];cache_services[0]=1;
 NAN_DATA_UNLOCK();
 assert(nan_app_register_paired_peer(config.peer,0,1,nd_pmk,sizeof(nd_pmk),1)==ESP_ERR_NO_MEM);
 assert(cache_services[0]==1&&cache_services[1]==1);wifi_task=false;
 unsigned calls=native_calls;
 assert(esp32_mquickjs_wifi_nan_pairing_start(&config,&result)==ESP_ERR_INVALID_STATE&&native_calls==calls);
 release();assert(!allocations&&esp32_mquickjs_wifi_nan_pairing_service_idle(2)==ESP_OK);
 config.verification=false;config.credential_id=0;config.pincode=123456;
 assert(!esp32_mquickjs_wifi_nan_pairing_start(&config,&result));
 assert(!s_esp32qjs_pairing_binding->config.pincode);
 uint8_t nik2[16]={2},new_npk[32]={0x71},hash2[6]={2};uint32_t committed=0;
 wifi_task=true;NAN_DATA_LOCK();nan_app_update_peer_creds(nik2,new_npk,hash2,1);NAN_DATA_UNLOCK();wifi_task=false;
 assert(s_nan_ctx.peer_creds[1].npk[0]==0xff); /* Staging must not overwrite a previous good key. */
 assert(esp32_mquickjs_wifi_nan_pairing_commit(snapshot.identity,&committed)==ESP_ERR_INVALID_STATE&&!committed);
 snapshot.paired=true;snapshot.traffic_pending=true;
 assert(esp32_mquickjs_wifi_nan_pairing_commit(snapshot.identity,&committed)==ESP_ERR_INVALID_STATE&&!committed);
 snapshot.traffic_pending=false;
 next_credential=0;assert(esp32_mquickjs_wifi_nan_pairing_commit(snapshot.identity,&committed)==ESP_ERR_NO_MEM);
 assert(s_nan_ctx.peer_creds[1].npk[0]==0xff);next_credential=201;
 assert(!esp32_mquickjs_wifi_nan_pairing_commit(snapshot.identity,&committed)&&committed==201);
 assert(s_nan_ctx.peer_creds[1].npk[0]==0x71&&!esp32_mquickjs_wifi_nan_pairing_commit(snapshot.identity,&committed)&&committed==201);
 esp32_mquickjs_wifi_nan_credentials_t list;
 assert(!esp32_mquickjs_wifi_nan_pairing_credentials(2,&list)&&list.count==1&&list.entries[0].identity==201);
 assert(!esp32_mquickjs_wifi_nan_pairing_credentials(1,&list)&&list.count==1&&list.entries[0].identity==101);
 release();assert(!allocations);
 config.verification=true;config.pincode=0;config.credential_id=102;
 assert(esp32_mquickjs_wifi_nan_pairing_start(&config,&result)==ESP_ERR_INVALID_ARG); /* Replaced ID. */
 native_clock+=1000000;
 assert(!esp32_mquickjs_wifi_nan_pairing_credentials(2,&list)&&!list.count);
 config.credential_id=201;assert(esp32_mquickjs_wifi_nan_pairing_start(&config,&result)==ESP_ERR_INVALID_ARG); /* Expired. */
 /* Exercise the production cleanup attributes without synthesizing secrets. */
 uint8_t nik[16]={1},key[32]={2},plain[64]={3};
 esp32qjs_pairing_clear_nik(&nik);esp32qjs_pairing_clear_npk(&key);esp32qjs_pairing_clear_plain(&plain);
 assert(!nik[0]&&!key[0]&&!plain[0]);return 0;
}
'''
