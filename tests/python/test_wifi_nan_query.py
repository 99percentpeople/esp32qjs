"""Deferred production NAN cache queries, exact Radio tokens and bounded reads.

Only SDK storage/RTOS boundaries are supplied here. The query, matching and
conversion implementations are read from production files, never recreated.
"""
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest

BASE = Path(__file__).resolve().parents[2] / 'components/esp32_mquickjs'


def query_types():
    # Public esp_nan.h metadata layout; C5 production compilation separately
    # checks the actual SDK definition (security/pairing do not change it).
    source = r'''
#if CONFIG_ESP_WIFI_NAN_SYNC_ENABLE
#define NAN_MAX_PEERS_RECORD 15
#define ESP_NAN_PUBLISH 2
#define ESP_NAN_SUBSCRIBE 1
struct nan_peer_record {
 uint8_t peer_svc_id,own_svc_id,peer_nmi[6],peer_svc_type,ndp_id,peer_ndi[6];
};
#endif
'''
    source += (BASE / 'internal/esp32_mquickjs_wifi_nan_query.h').read_text()
    return re.sub(r'^\s*#(?:include[^\n]*|pragma once)\n', '', source, flags=re.M)


def compile_and_run(test, code):
    cc = shutil.which('cc')
    if not cc:
        test.skipTest('Host C compiler unavailable')
    with tempfile.TemporaryDirectory() as folder:
        src, exe = Path(folder) / 'case.c', Path(folder) / 'case'
        src.write_text(code)
        result = subprocess.run([cc, '-std=gnu11', '-Wall', '-Wextra', '-Werror',
            str(src), '-o', str(exe)], text=True, capture_output=True, timeout=60)
        test.assertEqual(result.returncode, 0, result.stderr)
        result = subprocess.run([str(exe)], text=True, capture_output=True, timeout=10)
        test.assertEqual(result.returncode, 0, result.stderr)


class NanQuery(unittest.TestCase):
    def test_actual_native_snapshot_service_scope_zeroing_limits_and_corruption(self):
        production = (BASE / 'src/modules/wifi_nan/esp32_mquickjs_wifi_nan_query_sdk.inc').read_text()
        production = re.sub(r'^#include[^\n]*\n', '', production, flags=re.M)
        compile_and_run(self, SDK_PREFIX + query_types() + SDK_STORAGE + production + SDK_MAIN)

    def test_actual_radio_query_rejects_stale_identity_closing_and_usd(self):
        from test_wifi_nan_radio_retirement import function
        production = (BASE / 'src/modules/wifi_radio/esp32_mquickjs_wifi_nan_radio.inc').read_text()
        code = SDK_PREFIX + query_types() + RADIO_BOUNDARY
        code += function(production, 'wifi_radio_nan_exact_locked')
        code += function(production, 'esp32_mquickjs_wifi_radio_nan_query')
        compile_and_run(self, code + RADIO_MAIN)


SDK_PREFIX = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/queue.h>
#define CONFIG_ESP_WIFI_NAN_SYNC_ENABLE 1
#define ESP_WIFI_MAX_SVC_NAME_LEN 256
#define ESP_WIFI_NAN_MAX_SVC_SUPPORTED 2
#define ESP_WIFI_NAN_DATAPATH_MAX_PEERS 2
#define ESP_OK 0
#define ESP_ERR_INVALID_ARG 101
#define ESP_ERR_INVALID_STATE 102
#define ESP_ERR_NOT_FOUND 107
#define ESP_ERR_NOT_SUPPORTED 105
typedef int esp_err_t;
'''

SDK_STORAGE = r'''
struct peer_svc_info {SLIST_ENTRY(peer_svc_info) next;uint8_t svc_id,own_svc_id,type,peer_nmi[6];};
struct own_svc_info {char svc_name[256];uint8_t svc_id,type,num_peer_records;SLIST_HEAD(,peer_svc_info) peer_list;};
struct ndl_info {uint8_t ndp_id,peer_ndi[6],peer_nmi[6],publisher_id,own_role;};
static struct {struct own_svc_info own_svc[2];struct ndl_info ndl[2];}s_nan_ctx;
static void *s_nan_data_lock=(void *)1;
static unsigned locks;
static bool locked,managed_match=true;
#define NAN_DATA_LOCK() do{assert(!locked);locked=true;++locks;}while(0)
#define NAN_DATA_UNLOCK() do{assert(locked);locked=false;}while(0)
static bool esp32_mquickjs_wifi_nan_ndp_peer_service(const uint8_t peer[6],uint8_t publisher,uint8_t service){
 assert(locked&&peer[0]==2&&publisher&&service);return managed_match;
}
static void assert_clear(const void *value,size_t n){for(size_t i=0;i<n;i++)assert(!((const uint8_t*)value)[i]);}
'''

SDK_MAIN = r'''
int main(void){
 struct own_svc_info *own=&s_nan_ctx.own_svc[0];own->svc_id=7;own->type=ESP_NAN_PUBLISH;strcpy(own->svc_name,"alpha");
 esp32_mquickjs_wifi_nan_query_t q={.kind=ESP32_MQUICKJS_NAN_QUERY_SERVICE,.service_name="alpha"};
 esp32_mquickjs_wifi_nan_query_result_t out;memset(&out,0xa5,sizeof(out));
 assert(!esp32_mquickjs_wifi_nan_sdk_query(&q,&out)&&out.service_id==7&&!out.peer_count);
 assert_clear(out.peers,sizeof(out.peers));assert(locks==1&&!locked);
 q.service_name=NULL;q.service_id=8;
 assert(esp32_mquickjs_wifi_nan_sdk_query(&q,&out)==ESP_ERR_NOT_FOUND);assert_clear(&out,sizeof(out));
 q.service_id=7;q.service_name="alpha";unsigned before=locks;
 assert(esp32_mquickjs_wifi_nan_sdk_query(&q,&out)==ESP_ERR_INVALID_ARG&&locks==before);
 q.service_name=NULL;q.kind=ESP32_MQUICKJS_NAN_QUERY_PEERS;
 struct peer_svc_info peers[16]={0};
 for(unsigned i=0;i<15;i++){
  peers[i]=(struct peer_svc_info){.svc_id=i+1,.own_svc_id=7,.type=ESP_NAN_SUBSCRIBE,.peer_nmi={2,3,4,5,6,7}};
  SLIST_INSERT_HEAD(&own->peer_list,&peers[i],next);++own->num_peer_records;
 }
 s_nan_ctx.ndl[0]=(struct ndl_info){.ndp_id=9,.publisher_id=8,.own_role=ESP_NAN_PUBLISH,.peer_nmi={2,3,4,5,6,7},.peer_ndi={2,9}};
 assert(!esp32_mquickjs_wifi_nan_sdk_query(&q,&out)&&out.peer_count==15);
 for(unsigned i=0;i<15;i++){assert(!out.peers[i].ndp_id);assert_clear(out.peers[i].peer_ndi,6);}
 q.kind=ESP32_MQUICKJS_NAN_QUERY_PEER;memcpy(q.peer,peers[0].peer_nmi,6);
 assert(!esp32_mquickjs_wifi_nan_sdk_query(&q,&out)&&!out.peers[0].ndp_id);
 s_nan_ctx.ndl[1]=s_nan_ctx.ndl[0];s_nan_ctx.ndl[1].publisher_id=7;s_nan_ctx.ndl[1].ndp_id=10;
 assert(!esp32_mquickjs_wifi_nan_sdk_query(&q,&out)&&out.peers[0].ndp_id==10);
 assert(out.peers[0].peer_svc_id==15&&out.peers[0].peer_ndi[1]==9);
 own->type=ESP_NAN_SUBSCRIBE;s_nan_ctx.ndl[1].own_role=ESP_NAN_SUBSCRIBE;s_nan_ctx.ndl[1].publisher_id=15;
 for(unsigned i=0;i<15;i++)peers[i].type=ESP_NAN_PUBLISH;
 managed_match=false;assert(!esp32_mquickjs_wifi_nan_sdk_query(&q,&out)&&!out.peers[0].ndp_id);
 managed_match=true;assert(!esp32_mquickjs_wifi_nan_sdk_query(&q,&out)&&out.peers[0].ndp_id==10);
 q.service_id=0;assert(!esp32_mquickjs_wifi_nan_sdk_query(&q,&out)&&out.service_id==7);
 q.peer[5]=8;assert(esp32_mquickjs_wifi_nan_sdk_query(&q,&out)==ESP_ERR_NOT_FOUND);assert_clear(&out,sizeof(out));
 q.peer[0]=0;q.peer[5]=0;memset(q.peer,0,6);before=locks;
 assert(esp32_mquickjs_wifi_nan_sdk_query(&q,&out)==ESP_ERR_INVALID_ARG&&before==locks);
 q.kind=ESP32_MQUICKJS_NAN_QUERY_PEERS;q.service_id=7;
 --own->num_peer_records;assert(esp32_mquickjs_wifi_nan_sdk_query(&q,&out)==ESP_ERR_INVALID_STATE);assert_clear(&out,sizeof(out));
 ++own->num_peer_records;SLIST_INSERT_HEAD(&own->peer_list,&peers[15],next);
 assert(esp32_mquickjs_wifi_nan_sdk_query(&q,&out)==ESP_ERR_INVALID_STATE);assert_clear(&out,sizeof(out));
 SLIST_REMOVE_HEAD(&own->peer_list,next);peers[0].next.sle_next=&peers[14];
 assert(esp32_mquickjs_wifi_nan_sdk_query(&q,&out)==ESP_ERR_INVALID_STATE);assert_clear(&out,sizeof(out));
 peers[0].next.sle_next=NULL;memset(own->svc_name,'x',sizeof(own->svc_name));
 assert(esp32_mquickjs_wifi_nan_sdk_query(&q,&out)==ESP_ERR_INVALID_STATE);assert_clear(&out,sizeof(out));
 s_nan_data_lock=NULL;before=locks;
 assert(esp32_mquickjs_wifi_nan_sdk_query(&q,&out)==ESP_ERR_INVALID_STATE&&before==locks);
 assert(!locked);
}
'''

RADIO_BOUNDARY = r'''
typedef struct{uint32_t identity,generation,lease_identity;int kind;}esp32_mquickjs_wifi_radio_operation_t;
typedef struct{uint32_t identity;bool acquired;}lease_t;
#define ESP32_MQUICKJS_WIFI_RADIO_OPERATION_NAN 3
static struct{esp32_mquickjs_wifi_radio_operation_t operation;uint32_t generation;}s_radio;
static struct{lease_t lease;struct{bool ready,closing,usd;}status;}binding,*s_nan_radio=&binding;
static bool locked;
static unsigned reads;
static int native_error;
static bool wifi_radio_lease_valid(const lease_t*l){return l->acquired;}
static void wifi_radio_operation_lock(void){assert(!locked);locked=true;}
static void wifi_radio_operation_unlock(void){assert(locked);locked=false;}
esp_err_t esp32_mquickjs_wifi_nan_sdk_query(const esp32_mquickjs_wifi_nan_query_t*q,esp32_mquickjs_wifi_nan_query_result_t*out){
 assert(locked&&q&&out);++reads;if(!native_error)out->service_id=7;return native_error;
}
'''

RADIO_MAIN = r'''
int main(void){
 esp32_mquickjs_wifi_radio_operation_t token={.identity=42,.generation=3,.lease_identity=77,.kind=3};
 s_radio.operation=token;s_radio.generation=3;binding.lease=(lease_t){77,true};binding.status.ready=true;
 esp32_mquickjs_wifi_nan_query_t q={.kind=ESP32_MQUICKJS_NAN_QUERY_SERVICE,.service_id=7};
 esp32_mquickjs_wifi_nan_query_result_t out;
 assert(!esp32_mquickjs_wifi_radio_nan_query(&token,&q,&out)&&reads==1&&out.service_id==7&&!locked);
 esp32_mquickjs_wifi_radio_operation_t stale=token;--stale.identity;
 assert(esp32_mquickjs_wifi_radio_nan_query(&stale,&q,&out)==ESP_ERR_INVALID_STATE&&!out.service_id);
 stale=token;--stale.generation;assert(esp32_mquickjs_wifi_radio_nan_query(&stale,&q,&out)==ESP_ERR_INVALID_STATE);
 stale=token;--stale.lease_identity;assert(esp32_mquickjs_wifi_radio_nan_query(&stale,&q,&out)==ESP_ERR_INVALID_STATE);
 stale=token;--stale.kind;assert(esp32_mquickjs_wifi_radio_nan_query(&stale,&q,&out)==ESP_ERR_INVALID_STATE);
 binding.lease.acquired=false;assert(esp32_mquickjs_wifi_radio_nan_query(&token,&q,&out)==ESP_ERR_INVALID_STATE);
 binding.lease.acquired=true;binding.status.closing=true;
 assert(esp32_mquickjs_wifi_radio_nan_query(&token,&q,&out)==ESP_ERR_INVALID_STATE);
 binding.status.closing=false;binding.status.usd=true;
 assert(esp32_mquickjs_wifi_radio_nan_query(&token,&q,&out)==ESP_ERR_NOT_SUPPORTED&&reads==1&&!locked);
 binding.status.usd=false;native_error=-71;
 assert(esp32_mquickjs_wifi_radio_nan_query(&token,&q,&out)==-71&&reads==2&&!out.service_id&&!locked);
 s_nan_radio=NULL;assert(esp32_mquickjs_wifi_radio_nan_query(&token,&q,&out)==ESP_ERR_INVALID_STATE&&reads==2);
}
'''
