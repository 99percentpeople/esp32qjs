"""Deferred production DPP worker lifecycle, credential copies and IPC ownership."""
from pathlib import Path
import re
import unittest
from test_wireless_control_regression import compile_run

ROOT = Path(__file__).resolve().parents[2]


def declarations(text):
    return re.sub(r'^#(?:include|pragma)[^\n]*$', '', text, flags=re.M)


class DppWorker(unittest.TestCase):
    def test_native_calls_copy_commit_cleanup_retry_and_unknown_handoff(self):
        internal = ROOT / 'components/esp32_mquickjs/internal'
        header = declarations((internal / 'esp32_mquickjs_wifi_dpp_connection.h').read_text())
        header += declarations((internal / 'esp32_mquickjs_wifi_dpp_result.h').read_text())
        header += declarations((internal / 'esp32_mquickjs_wifi_dpp_worker.h').read_text())
        source = declarations((ROOT / 'components/esp32_mquickjs/src/modules/wifi_dpp/esp32_mquickjs_wifi_dpp_worker.c').read_text())
        source = source.replace('_Static_assert(sizeof(dpp_ipc_config_t) == 12, "review DPP IPC ABI");', '')
        compile_run(self, TYPES + header + BOUNDARIES + source + MAIN)


TYPES = r'''
#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
#define CONFIG_ESP32_MQUICKJS_FEATURE_WIFI 1
#define CONFIG_ESP_WIFI_DPP_SUPPORT 1
enum {ESP_DPP_AKM_DPP=1,ESP_DPP_AKM_PSK=2,ESP_DPP_AKM_SAE=3};
typedef int esp_err_t;
typedef struct {unsigned char secret[100];}wifi_config_t;
typedef struct {unsigned char secret[100];}esp_dpp_config_data_t;
typedef struct {int unused;}wifi_event_action_tx_status_t;
#define ESP_OK 0
#define ESP_ERR_NO_MEM 0x101
#define ESP_ERR_INVALID_ARG 0x102
#define ESP_ERR_INVALID_STATE 0x103
#define ESP_ERR_INVALID_SIZE 0x104
#define ESP_ERR_NOT_FINISHED 0x10a
#define ESP_ERR_WIFI_NOT_INIT 0x3001
#define MALLOC_CAP_INTERNAL 1
#define MALLOC_CAP_8BIT 2
'''

BOUNDARIES = r'''
static void*allocation;
static size_t allocated_size;
static unsigned allocations,begins,bootstraps,listens,closes,releases,copies,commits;
static int ipc_mode,init_error,close_error,release_error,validate_error,select_error;
static unsigned selects;
static bool fail_alloc;
static esp32_mquickjs_wifi_dpp_result_status_t native;
static void esp32_mquickjs_wireless_secure_zero(void*p,size_t n){volatile uint8_t*q=p;while(n--)*q++=0;}
static void*heap_caps_calloc(size_t n,size_t size,unsigned caps){
 assert(!allocation&&n==1&&caps==3);if(fail_alloc)return NULL;
 allocation=calloc(n,size);assert(allocation);allocated_size=size;allocations++;return allocation;
}
static void heap_caps_free(void*p){assert(p==allocation&&allocations==1);
 for(size_t i=0;i<allocated_size;i++)assert(!((uint8_t*)p)[i]);free(p);allocation=NULL;allocations--;
}
esp_err_t esp32qjs_dpp_bootstrap_validate(const char*c,const char*k,const char*i){assert(c&&k&&i);return validate_error;}
esp_err_t esp32qjs_dpp_native_begin(uint64_t*id){assert(!*id);begins++;*id=41;
 native=(esp32_mquickjs_wifi_dpp_result_status_t){.identity=41,.attached=true,.retained=true,.reserved_bytes=999};return init_error;
}
esp_err_t esp32qjs_dpp_native_bootstrap(uint64_t id,const char*c,const char*k,const char*i){
 assert(id==41&&!strcmp(c,"1,6,11")&&!strcmp(k,"abcd")&&!strcmp(i,"label"));bootstraps++;return 0;
}
esp_err_t esp32_mquickjs_wifi_dpp_connection_prepare(const esp_dpp_config_data_t*r,esp32_mquickjs_wifi_dpp_auth_t a,
 esp32_mquickjs_wifi_dpp_auth_t*chosen,wifi_config_t*out){
 if(!r||r->secret[0]!=71)return ESP_ERR_INVALID_ARG;*chosen=a?a:ESP32_MQUICKJS_DPP_AUTH_CONNECTOR;
 memset(out,42,sizeof(*out));return 0;}
esp_err_t esp32qjs_dpp_native_select(uint64_t id,const esp_dpp_config_data_t*r){assert(id==41&&r&&r->secret[0]==71);selects++;return select_error;}
esp_err_t esp32qjs_dpp_native_check_connection(uint64_t id,uint8_t akm,const uint8_t bssid[6]){assert(id==41&&akm==1&&bssid[0]==2);return 0;}
esp_err_t esp32qjs_dpp_native_listen(uint64_t id){assert(id==41&&native.uri_length);listens++;return 0;}
esp_err_t esp32qjs_dpp_result_status(uint64_t id,esp32_mquickjs_wifi_dpp_result_status_t*out){assert(id==41);*out=native;return 0;}
esp_err_t esp32qjs_dpp_result_uri_copy(uint64_t id,char*out,size_t capacity){
 assert(id==41&&capacity>=10);if(!native.uri_available)return ESP_ERR_INVALID_STATE;memcpy(out,"DPP:K:x;;",10);copies++;return 0;
}
esp_err_t esp32qjs_dpp_result_uri_commit(uint64_t id){assert(id==41);native.uri_available=false;commits++;return 0;}
esp_err_t esp32qjs_dpp_result_config_copy(uint64_t id,unsigned index,esp_dpp_config_data_t*out){
 assert(id==41);if(index>=native.config_count)return ESP_ERR_INVALID_STATE;memset(out,71,sizeof(*out));copies++;return 0;
}
esp_err_t esp32qjs_dpp_result_configs_commit(uint64_t id){assert(id==41);native.configs_available=false;commits++;return 0;}
esp_err_t esp32qjs_dpp_native_close(uint64_t id){assert(id==41);closes++;if(close_error)return close_error;
 native.closing=true;native.sdk_retired=true;native.driver_retired=true;native.event_fenced=true;return 0;
}
esp_err_t esp32qjs_dpp_result_release(uint64_t id){assert(id==41&&native.sdk_retired);releases++;return release_error;}
'''

MAIN = r'''
static dpp_ipc_config_t pending;
int esp_wifi_ipc_internal(dpp_ipc_config_t*config,bool sync){
 assert(sync&&!config->arg_size&&config->arg==allocation);
 if(ipc_mode==1)return ESP_ERR_NO_MEM;
 if(ipc_mode==2){pending=*config;return 909;}
 return config->fn(config->arg);
}
static void zero_result(esp32_mquickjs_wifi_dpp_worker_t*w){
 for(size_t i=0;i<sizeof(w->result);i++)assert(!((uint8_t*)&w->result)[i]);
}
int main(void){
 esp32_mquickjs_wifi_dpp_worker_options_t options={.channels="1,6,11",.private_key_hex_der="abcd",.info="label",.has_key=true,.has_info=true};
 esp32_mquickjs_wifi_dpp_worker_t*w=NULL;
 validate_error=55;assert(esp32_mquickjs_wifi_dpp_worker_create(&options,&w)==55&&!w&&!allocations&&!begins);validate_error=0;
 fail_alloc=true;assert(esp32_mquickjs_wifi_dpp_worker_create(&options,&w)==ESP_ERR_NO_MEM&&!w);fail_alloc=false;
 assert(!esp32_mquickjs_wifi_dpp_worker_create(&options,&w));assert(!esp32_mquickjs_wifi_dpp_worker_prepare(w)&&begins==1&&bootstraps==1);
 for(size_t i=0;i<sizeof(w->options);i++)assert(!((uint8_t*)&w->options)[i]);
 assert(esp32_mquickjs_wifi_dpp_worker_listen(w)==ESP_ERR_NOT_FINISHED&&!listens);
 native.uri_length=9;native.uri_available=true;assert(!esp32_mquickjs_wifi_dpp_worker_listen(w)&&listens==1);
 assert(!esp32_mquickjs_wifi_dpp_worker_listen(w)&&listens==1);
 char uri[12];assert(esp32_mquickjs_wifi_dpp_worker_uri(w,uri,2,false)==ESP_ERR_INVALID_SIZE&&native.uri_available);zero_result(w);
 assert(!esp32_mquickjs_wifi_dpp_worker_uri(w,uri,sizeof(uri),false)&&!strcmp(uri,"DPP:K:x;;"));zero_result(w);
 assert(!esp32_mquickjs_wifi_dpp_worker_uri(w,NULL,0,true)&&!native.uri_available);
 native.terminal=true;native.configs_available=true;native.config_count=2;
 esp_dpp_config_data_t config;assert(!esp32_mquickjs_wifi_dpp_worker_config(w,1,&config)&&config.secret[0]==71);zero_result(w);
 assert(esp32_mquickjs_wifi_dpp_worker_config(w,2,&config)==ESP_ERR_INVALID_STATE);zero_result(w);
 assert(!esp32_mquickjs_wifi_dpp_worker_configs_commit(w)&&!native.configs_available);
 close_error=66;assert(esp32_mquickjs_wifi_dpp_worker_finish_capture(w)==66&&!w->capture_retired);
 close_error=0;assert(!esp32_mquickjs_wifi_dpp_worker_finish_capture(w)&&w->capture_retired);
 assert(!esp32_mquickjs_wifi_dpp_worker_config(w,0,&config));
 unsigned before=closes;release_error=77;assert(esp32_mquickjs_wifi_dpp_worker_close(w)==77&&closes==before&&!w->retired);
 release_error=0;assert(!esp32_mquickjs_wifi_dpp_worker_close(w)&&closes==before&&w->retired);
 assert(!esp32_mquickjs_wifi_dpp_worker_release(&w)&&!w&&!allocations);
 /* Independent selection begins only after capture and old result release. */
 for(unsigned failure=0;failure<4;failure++){
  assert(!esp32_mquickjs_wifi_dpp_worker_create(&options,&w));assert(!esp32_mquickjs_wifi_dpp_worker_prepare(w));
  native.terminal=true;assert(!esp32_mquickjs_wifi_dpp_worker_finish_capture(w));
  wifi_config_t station;esp32_mquickjs_wifi_dpp_auth_t chosen;memset(&config,71,sizeof(config));
  if(failure==1)release_error=77;if(failure==2)init_error=88;if(failure==3)select_error=99;
  unsigned old_begins=begins,old_selects=selects;
  int e=esp32_mquickjs_wifi_dpp_worker_select(w,&config,0,&chosen,&station);
  assert(e==(failure==1?77:failure==2?88:failure==3?99:0));assert(w->selection_attempted);
  assert(begins==old_begins+(failure!=1));assert(selects==old_selects+(failure==0||failure==3));
  if(!failure){assert(w->connection_selected&&station.secret[0]==42&&chosen==ESP32_MQUICKJS_DPP_AUTH_CONNECTOR);zero_result(w);}
  else for(size_t i=0;i<sizeof(station);i++)assert(!((uint8_t*)&station)[i]);
  release_error=init_error=select_error=0;
  assert(esp32_mquickjs_wifi_dpp_worker_select(w,&config,0,&chosen,&station)==ESP_ERR_INVALID_STATE);
  assert(!esp32_mquickjs_wifi_dpp_worker_close(w));assert(!esp32_mquickjs_wifi_dpp_worker_release(&w));
 }
 /* A failed initialization can own native storage and must close it. */
 assert(!esp32_mquickjs_wifi_dpp_worker_create(&options,&w));init_error=88;
 assert(esp32_mquickjs_wifi_dpp_worker_prepare(w)==88&&w->identity==41);init_error=0;
 assert(!esp32_mquickjs_wifi_dpp_worker_close(w));assert(!esp32_mquickjs_wifi_dpp_worker_release(&w));
 /* Definite pre-dispatch failure never reserved a native identity. */
 assert(!esp32_mquickjs_wifi_dpp_worker_create(&options,&w));ipc_mode=1;before=begins;
 assert(esp32_mquickjs_wifi_dpp_worker_prepare(w)==ESP_ERR_NO_MEM&&begins==before&&!w->identity);ipc_mode=0;
 assert(!esp32_mquickjs_wifi_dpp_worker_close(w));assert(!esp32_mquickjs_wifi_dpp_worker_release(&w));
 /* Unconfirmed dispatch retains every argument/result; a late receipt is
  * diagnostic evidence, not permission to repeat commands or free storage. */
 assert(!esp32_mquickjs_wifi_dpp_worker_create(&options,&w));ipc_mode=2;
 assert(esp32_mquickjs_wifi_dpp_worker_prepare(w)==909&&w->handoff_unknown);
 pending.fn(pending.arg);esp32_mquickjs_wifi_dpp_worker_status_t status;
 assert(!esp32_mquickjs_wifi_dpp_worker_status(w,&status)&&status.handoff_unknown&&!status.native.identity);
 assert(esp32_mquickjs_wifi_dpp_worker_close(w)==ESP_ERR_INVALID_STATE);
 assert(esp32_mquickjs_wifi_dpp_worker_release(&w)==ESP_ERR_INVALID_STATE&&allocations==1);
 return 0;
}
'''
