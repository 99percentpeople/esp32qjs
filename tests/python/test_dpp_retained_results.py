"""Deferred production result ownership, SDK cleanup suffix and event fence.

Native driver/event-loop calls are controllable boundaries. No RF or RTOS
success is implied by this fixture, and execution remains deferred this wave.
"""
import os
from pathlib import Path
import re
import sys
import unittest
from test_wireless_control_regression import compile_run

ROOT = Path(__file__).resolve().parents[2]
PARTS = ROOT / 'components/esp32_mquickjs/src/modules/wifi_dpp'


def without_includes(text):
    return re.sub(r'^#(?:include|pragma once).*\n', '', text, flags=re.M)


class DppRetainedResults(unittest.TestCase):
    def test_copy_commit_terminal_identity_partial_cleanup_and_queue_fence(self):
        sdk_path = os.environ.get('IDF_PATH')
        if not sdk_path:
            self.skipTest('Set IDF_PATH to the reviewed SDK')
        sys.path.insert(0, str(ROOT / 'scripts'))
        from patch_idf_dpp import function
        from test_dpp_config_transaction import sdk_struct
        public = (Path(sdk_path) / 'components/esp_wifi/include/esp_wifi_types_generic.h').read_text()
        header = without_includes((ROOT / 'components/esp32_mquickjs/internal/esp32_mquickjs_wifi_dpp_result.h').read_text())
        source = TYPES + sdk_struct(public, 'esp_dpp_config_data_t')
        source += sdk_struct(public, 'wifi_event_dpp_config_received_t') + header
        source += sdk_struct((ROOT / 'components/esp32_mquickjs/internal/esp32_mquickjs_wifi_chm_timer.h').read_text(),
            'esp32qjs_wifi_chm_timer_status_t') + BOUNDARIES
        source += function((PARTS / 'esp32_mquickjs_dpp_config.inc').read_text(), 'esp32qjs_dpp_config_valid')
        source += without_includes((PARTS / 'esp32_mquickjs_dpp_result.inc').read_text())
        source += (PARTS / 'esp32_mquickjs_dpp_deinit.inc').read_text()
        compile_run(self, source + MAIN)


TYPES = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
typedef int esp_err_t;
typedef int esp_event_base_t;
typedef struct {int unused;}wifi_event_action_tx_status_t;
typedef struct {unsigned char unused[16];}wifi_config_t;
#define ESP_OK 0
#define ESP_ERR_INVALID_STATE 1
#define ESP_ERR_INVALID_ARG 2
#define ESP_ERR_INVALID_SIZE 3
#define ESP_ERR_NO_MEM 4
#define ESP_ERR_NOT_FINISHED 5
#define MAX_SSID_LEN 32
#define MAX_PASSPHRASE_LEN 64
#define ESP_DPP_MAX_CONNECTOR_LEN 512
#define ESP_DPP_MAX_KEY_LEN 128
#define ESP_DPP_MAX_CONFIG_COUNT 3
#define ESP_DPP_AKM_DPP 1
#define ESP_DPP_AKM_PSK_SAE_DPP 6
#define WIFI_EVENT 1
#define WIFI_EVENT_DPP_FAILED 2
#define WIFI_EVENT_DPP_CFG_RECVD 3
#define WIFI_EVENT_ACTION_TX_STATUS 4
#define WIFI_EVENT_ROC_DONE 5
#define WIFI_IF_STA 0
#define WIFI_ROC_CANCEL 1
#define WIFI_OFFCHAN_TX_CANCEL 2
#define WIFI_SECOND_CHAN_NONE 0
#define WPS_OWNER_NONE 0
#define CONFIG_ESP_WIFI_WPS_SOFTAP_REGISTRAR 0
#define ESP_EVENT_DEFINE_BASE(name) static const int name=100
#define os_memcpy memcpy
#define os_bzero(p,n) memset(p,0,n)
enum dpp_akm {DPP_AKM_UNKNOWN,DPP_AKM_DPP,DPP_AKM_PSK,DPP_AKM_SAE,DPP_AKM_PSK_SAE,DPP_AKM_SAE_DPP,DPP_AKM_PSK_SAE_DPP};
typedef struct {int ifx,type,sec_channel;void *rx_cb;uint8_t op_id,channel;}wifi_roc_req_t;
typedef wifi_roc_req_t wifi_action_tx_req_t;
'''

BOUNDARIES = r'''
static int depth,live,fail_alloc,unregister_error,post_error,quiescent_error,drain_error;
static unsigned unregister_fence,heap_frees,post_count,cancels;
static bool tx_held,tx_cancelled;
static bool wifi_task=true,wps_held;
static bool s_dpp_init_pending,s_dpp_deinit_dispatch_pending;
static unsigned s_dpp_async_count,s_dpp_async_active;
static atomic_bool s_dpp_init_done,dpp_shutting_down,roc_in_progress;
static struct {bool dpp_deinit_pending,dpp_listen_ongoing;void*dpp_global,*dpp_config_store;}s_dpp_ctx;
static void *s_action_rx_cb=(void*)0x1234;
static int dpp_api_lock(void){depth++;return 0;}
static int dpp_api_unlock(void){assert(depth);depth--;return 0;}
static bool current_task_is_wifi_task(void){return wifi_task;}
static bool esp32qjs_wps_native_held(void){return wps_held;}
static int wps_get_owner(void){return 0;}
static void *os_zalloc(size_t n){if(fail_alloc){fail_alloc=0;return NULL;}void*p=calloc(1,n);assert(p);live++;return p;}
static void bin_clear_free(void*p,size_t n){if(p){volatile unsigned char*q=p;for(size_t i=0;i<n;i++)q[i]=0;live--;free(p);}}
static bool dpp_akm_dpp(enum dpp_akm x){return x==1||x==5||x==6;}
/* This fixture covers result/SDK teardown; ROC's real producer/retirement
 * implementation is exercised separately by test_dpp_roc_control.py. */
static bool esp32qjs_dpp_roc_held(void){return false;}
static void esp32qjs_dpp_roc_status_locked(esp32_mquickjs_wifi_dpp_result_status_t*out){(void)out;}
static int esp32qjs_dpp_roc_cancel_locked(void){return 0;}
static bool esp32qjs_dpp_tx_held(void){return tx_held;}
static void esp32qjs_dpp_tx_status_locked(esp32_mquickjs_wifi_dpp_result_status_t*out){(void)out;}
static void esp32qjs_wifi_chm_timer_status(esp32qjs_wifi_chm_timer_status_t*out){
 *out=(esp32qjs_wifi_chm_timer_status_t){.post_failures=2,.timers_held=2};
}
static int esp32qjs_dpp_tx_cancel_locked(void){
 if(!tx_held)return 0;
 if(!tx_cancelled){tx_cancelled=true;cancels++;}
 if(quiescent_error)return quiescent_error;
 tx_held=false;return 0;
}
static int esp32qjs_dpp_bootstrap_cancel_locked(void){return 0;}
static void esp_dpp_cancel_timeouts(void){}
static int esp32qjs_dpp_async_drain_locked(void){return drain_error;}
static void dpp_stop_internal(void){}
static int esp32_mquickjs_wifi_action_sdk_quiescent(void){return quiescent_error;}
static int esp_event_handler_register(int base,int id,void(*fn)(void*,int,int,void*),void*arg){(void)base;(void)id;(void)fn;(void)arg;assert(!depth);return 0;}
static int esp_event_handler_unregister(int base,int id,void(*fn)(void*,int,int,void*)){
 (void)fn;assert(!depth&&base==100&&!id);unregister_fence++;return unregister_error;
}
static int esp_event_post(int base,int id,const void*data,size_t size,int wait){assert(base==100&&!id&&data&&size==8&&!wait&&depth);post_count++;return post_error;}
static void dpp_global_deinit(void*p){assert(p);heap_frees++;}
static void dpp_config_store_deinit(void*p){assert(p);heap_frees++;}
static void esp_wifi_sta_notify_dpp_config_set_internal(bool set){assert(!set);}
'''

MAIN = r'''
static int close_id(uint64_t id){return esp_dpp_deinit((void*)(uintptr_t)(id>>32),(void*)(uintptr_t)(uint32_t)id);}
int main(void){
 uint64_t id=0;fail_alloc=1;assert(esp32qjs_dpp_result_reserve(&id)==ESP_ERR_NO_MEM&&!id&&!live);
 wps_held=true;assert(esp32qjs_dpp_result_reserve(&id)==ESP_ERR_INVALID_STATE);wps_held=false;
 assert(!esp32qjs_dpp_result_reserve(&id)&&id&&esp32qjs_dpp_result_held());
 assert(esp32qjs_dpp_result_discard_unbound(id+1)==ESP_ERR_INVALID_STATE);
 dpp_api_lock();assert(!esp32qjs_dpp_result_bind_locked(id));
 assert(!esp32qjs_dpp_result_uri_locked("DPP:K:x;;",9));
 assert(esp32qjs_dpp_result_uri_locked("DPP:K:y;;",9)==ESP_ERR_INVALID_STATE);dpp_api_unlock();
 char uri[32];assert(esp32qjs_dpp_result_uri_copy(id,uri,9)==ESP_ERR_INVALID_SIZE);
 assert(!esp32qjs_dpp_result_uri_copy(id,uri,sizeof(uri))&&!strcmp(uri,"DPP:K:x;;"));
 assert(esp32qjs_dpp_result_uri_commit(id+1)==ESP_ERR_INVALID_STATE);
 assert(!esp32qjs_dpp_result_uri_commit(id));assert(esp32qjs_dpp_result_uri_commit(id)==ESP_ERR_INVALID_STATE);
 size_t size=sizeof(wifi_event_dpp_config_received_t)+sizeof(esp_dpp_config_data_t);
 wifi_event_dpp_config_received_t*event=calloc(1,size);assert(event);event->total_conf=1;
 esp_dpp_config_data_t*row=&event->configs[0];row->ssid_len=3;memcpy(row->ssid,"abc",3);row->akm=1;
 row->connector_len=2;memcpy(row->connector,"xy",2);row->net_access_key_len=2;row->net_access_key[0]=71;
 row->c_sign_key_len=1;row->c_sign_key[0]=72;row->net_access_key_expiry=123456;
 dpp_api_lock();assert(!esp32qjs_dpp_result_configs_locked(event));esp32qjs_dpp_result_failure_locked(42);dpp_api_unlock();
 esp_dpp_config_data_t out;assert(!esp32qjs_dpp_result_config_copy(id,0,&out));assert(out.net_access_key[0]==71&&out.net_access_key_expiry==123456);
 assert(!esp32qjs_dpp_result_configs_commit(id));assert(!esp32qjs_dpp_result_config_copy(id,0,&out));
 esp32_mquickjs_wifi_dpp_result_status_t status;assert(!esp32qjs_dpp_result_status(id,&status));
 assert(status.terminal&&status.event_id==WIFI_EVENT_DPP_CFG_RECVD&&!status.error&&status.duplicate_results==1);
 assert(esp32qjs_dpp_result_release(id)==ESP_ERR_INVALID_STATE);
 s_dpp_ctx.dpp_global=(void*)1;s_dpp_ctx.dpp_config_store=(void*)2;s_dpp_init_done=true;
 s_dpp_tx_submitted=true;tx_held=true;
 s_dpp_async_active=1;assert(close_id(id)==ESP_ERR_NOT_FINISHED&&!s_dpp_result->status.closing);s_dpp_async_active=0;
 quiescent_error=44;assert(close_id(id)==44&&!heap_frees&&!unregister_fence&&cancels==1);
 quiescent_error=0;post_error=46;assert(close_id(id)==46&&!heap_frees&&!unregister_fence&&cancels==1);
 post_error=0;assert(close_id(id)==ESP_ERR_NOT_FINISHED&&post_count==2&&!heap_frees);
 uint64_t stale=id+1;esp32qjs_dpp_fence_handler(NULL,ESP32QJS_DPP_PRIVATE_EVENT,0,&stale);
 assert(close_id(id)==ESP_ERR_NOT_FINISHED&&!heap_frees&&post_count==2);
 esp32qjs_dpp_fence_handler(NULL,ESP32QJS_DPP_PRIVATE_EVENT,0,&id);
 unregister_error=45;assert(close_id(id)==45&&!heap_frees&&unregister_fence==1);
 unregister_error=0;
 assert(!close_id(id)&&heap_frees==2&&esp32qjs_dpp_result_held());
 assert(!close_id(id)&&heap_frees==2&&unregister_fence==2);assert(!esp32qjs_dpp_result_config_copy(id,0,&out));
 assert(!esp32qjs_dpp_result_release(id)&&!live&&!esp32qjs_dpp_result_held());free(event);
 uint64_t old=id;id=0;assert(!esp32qjs_dpp_result_reserve(&id)&&id>old);
 assert(esp32qjs_dpp_result_configs_commit(old)==ESP_ERR_INVALID_STATE);
 assert(!esp32qjs_dpp_result_discard_unbound(id));
 s_dpp_result_last_identity=UINT64_MAX;id=0;assert(esp32qjs_dpp_result_reserve(&id)==ESP_ERR_NO_MEM&&!live&&!depth);
 return 0;
}
'''
