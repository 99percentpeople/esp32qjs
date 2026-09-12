"""Deferred AP close regressions using production helpers and SDK functions.

AST only during implementation. Callback/driver boundaries inject scheduling and
errors; no independent cleanup model or assertion of EAP/native drain.
"""
import os
from pathlib import Path
import sys
import unittest
from test_idf_wps_ap_result import WpsAPResult
from test_wireless_control_regression import compile_run

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts'))
from patch_idf_wps import function
from patch_idf_wps_registrar import patch_source


class WpsAPClose(unittest.TestCase):
    run_case = WpsAPResult.run_case

    def test_native_protocol_activity_can_finish_after_result_but_not_after_close(self):
        self.run_case(r'''
int main(void){
 int context=1;uint8_t peer[6]={2,0,0,0,0,1};
 assert(esp32qjs_wps_ap_result_bind(&context)==ESP_OK);
 uint32_t id=esp32qjs_wps_ap_result_identity(&context);
 assert(esp32qjs_wps_ap_result_peer_error(id,-73,peer)==ESP_OK);
 assert(s_wps_ap_result->status.terminal && s_wps_ap_result->status.error==-73);
 assert(!esp32qjs_wps_ap_result_context(id));
 assert(esp32qjs_wps_ap_result_native_context(id)==&context);
 assert(esp32qjs_wps_ap_result_activity_enter(id)==&context);
 assert(esp32qjs_wps_ap_result_busy(&context));
 assert(esp32qjs_wps_ap_result_peer_error(id,-74,peer)==ESP_ERR_INVALID_STATE);
 assert(s_wps_ap_result->status.error==-73);
 assert(esp32qjs_wps_ap_result_prepare_close(&context)==ESP_ERR_INVALID_STATE);
 assert(!esp32qjs_wps_ap_result_native_context(id));
 esp32qjs_wps_ap_result_callback_leave(id);
 assert(!esp32qjs_wps_ap_result_busy(&context));
 assert(esp32qjs_wps_ap_result_prepare_close(&context)==ESP_OK);
 esp32qjs_wps_ap_result_detach(&context,ESP_OK);assert(!live);
 return 0;
}
''')

    def test_each_close_failure_retries_only_unfinished_suffix(self):
        self.run_case(r'''
int main(void) {
 int context=1;
 for(int stage=0;stage<ESP32QJS_WPS_AP_CLOSE_PREPARED;stage++) {
  memset(cleanup_calls,0,sizeof(cleanup_calls));fail_stage=stage;
  assert(esp32qjs_wps_ap_result_bind(&context)==ESP_OK);
  uint32_t id=esp32qjs_wps_ap_result_identity(&context);
  assert(esp32qjs_wps_ap_result_prepare_close(&context)==cleanup_failure);
  esp32_mquickjs_wifi_wps_ap_result_status_t state;
  assert(esp32qjs_wps_ap_result_status(id,&state)==ESP_OK);
  assert(state.closing && !state.close_prepared && state.cleanup_stage==stage);
  assert(state.cleanup_error==cleanup_failure && state.sdk_attached && live==1);
  assert(!esp32qjs_wps_ap_result_deinit_allowed(&context));
  assert(!esp32qjs_wps_ap_result_context(id) && !esp32qjs_wps_ap_result_can_bind());
  for(int i=0;i<ESP32QJS_WPS_AP_CLOSE_PREPARED;i++)assert(cleanup_calls[i]==(unsigned)(i<=stage));
  fail_stage=-1;assert(esp32qjs_wps_ap_result_prepare_close(&context)==ESP_OK);
  assert(esp32qjs_wps_ap_result_deinit_allowed(&context));
  assert(esp32qjs_wps_ap_result_prepare_close(&context)==ESP_OK);
  for(int i=0;i<ESP32QJS_WPS_AP_CLOSE_PREPARED;i++)assert(cleanup_calls[i]==(unsigned)(i==stage?2:1));
  esp32qjs_wps_ap_result_detach(&context,ESP_OK);assert(!live);
 }
 return 0;
}
''')

    def test_reentrant_close_retains_context_and_exact_leave_after_terminal(self):
        self.run_case(r'''
int main(void) {
 int context=1;assert(esp32qjs_wps_ap_result_bind(&context)==ESP_OK);
 uint32_t id=esp32qjs_wps_ap_result_identity(&context);
 assert(esp32qjs_wps_ap_result_callback_enter(id)==&context);
 assert(esp32qjs_wps_ap_result_event(&context,WIFI_EVENT_AP_WPS_RG_TIMEOUT,NULL,0)==ESP_OK);
 assert(esp32qjs_wps_ap_result_prepare_close(&context)==ESP_ERR_INVALID_STATE);
 assert(s_wps_ap_result->status.callback_depth==1 && live==1);
 for(unsigned i=0;i<ESP32QJS_WPS_AP_CLOSE_PREPARED;i++)assert(!cleanup_calls[i]);
 esp32qjs_wps_ap_result_detach(&context,ESP_OK);assert(live==1 && s_wps_ap_result->status.sdk_attached);
 esp32qjs_wps_ap_result_callback_leave(id+1);assert(s_wps_ap_result->status.callback_depth==1);
 esp32qjs_wps_ap_result_callback_leave(id);assert(!s_wps_ap_result->status.callback_depth);
 assert(!esp32qjs_wps_ap_result_callback_enter(id));
 assert(esp32qjs_wps_ap_result_prepare_close(&context)==ESP_OK);
 assert(s_wps_ap_result->status.error==ESP_ERR_TIMEOUT && !s_wps_ap_result->status.cleanup_error);
 esp32qjs_wps_ap_result_detach(&context,ESP_OK);assert(!live);
 return 0;
}
''')

    def test_retained_prefix_is_not_permission_to_free_and_depth_fault_is_sticky(self):
        self.run_case(r'''
int main(void) {
 int context=1;uint32_t id=0;assert(esp32qjs_wps_ap_result_reserve(&id)==ESP_OK);
 assert(esp32qjs_wps_ap_result_bind(&context)==ESP_OK);
 assert(esp32qjs_wps_ap_result_prepare_close(&context)==ESP_OK);
 assert(!esp32qjs_wps_ap_result_deinit_allowed(&context) && live==1);
 assert(esp32qjs_wps_ap_result_discard_unbound(id)==ESP_ERR_INVALID_STATE);
 esp32qjs_wps_ap_result_callback_leave(id);assert(s_wps_ap_result->status.tracking_fault);
 assert(esp32qjs_wps_ap_result_prepare_close(&context)==ESP_ERR_INVALID_STATE);
 esp32qjs_wps_ap_result_detach(&context,ESP_OK);assert(s_wps_ap_result->status.sdk_attached);
 esp32qjs_wps_ap_result_free(); /* Fixture teardown, not managed release. */
 assert(esp32qjs_wps_ap_result_bind(&context)==ESP_OK);
 id=esp32qjs_wps_ap_result_identity(&context);
 s_wps_ap_result->status.callback_depth=UINT32_MAX;
 assert(!esp32qjs_wps_ap_result_callback_enter(id) && s_wps_ap_result->status.tracking_fault);
 esp32qjs_wps_ap_result_free();return 0;
}
''')


class WpsAPHostClose(unittest.TestCase):
    def test_original_ignores_close_failure_patched_retains_ap_and_station_is_untouched(self):
        sdk = os.environ.get('IDF_PATH')
        if not sdk:
            self.skipTest('Set IDF_PATH to the reviewed ESP-IDF')
        relative = 'esp_supplicant/src/esp_hostap.c'
        original = (Path(sdk) / 'components/wpa_supplicant' / relative).read_bytes()
        for patched in (False, True):
            source = patch_source(relative, original).decode() if patched else original.decode()
            code = HOST_BOUNDARIES
            if patched:
                code += function(source, 'esp32qjs_hostap_close_wps')
            code += function(source, 'hostapd_cleanup') + function(source, 'hostap_deinit')
            code += r'''
int main(void) {
 struct hostapd_config conf={0};struct hostapd_data h={.conf=&conf,.wpa_auth=&conf};global_hapd=&h;
 close_error=0x456;
 bool result=hostap_deinit(&h);
 assert(result==!PATCHED && frees==(PATCHED?0:1));
 if(PATCHED){
  assert(!unset && h.conf==&conf && h.wpa_auth==&conf && global_hapd==&h);
  hostapd_cleanup(&h);assert(!frees && h.conf==&conf);
  close_error=0;assert(hostap_deinit(&h) && frees==1 && !global_hapd);
  global_hapd=&h;owner=WPS_OWNER_ENROLLEE;bound=false;int before=disable_calls;
  assert(hostap_deinit(&h) && disable_calls==before && frees==2);
 }
 assert(!public_disable);return 0;
}
'''.replace('PATCHED', '1' if patched else '0')
            with self.subTest(patched=patched): compile_run(self, code)


HOST_BOUNDARIES = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#define CONFIG_WPS_REGISTRAR 1
#define ESP_OK 0
#define ESP_ERR_INVALID_STATE 0x103
#define WPS_TYPE_DISABLE 0
#define WPS_STATUS_DISABLE 0
#define WIFI_PASSWORD_LEN_MAX 65
#define WIFI_APPIE_WPA 1
#define WIFI_APPIE_ASSOC_RESP 2
#define MSG_ERROR 0
#define wpa_printf(...) ((void)0)
enum{WPS_OWNER_NONE,WPS_OWNER_ENROLLEE,WPS_OWNER_REGISTRAR};
struct hostapd_config{struct{char *wpa_passphrase;}ssid;};
struct hostapd_data{struct hostapd_config *conf;void *wpa_auth;};
static struct hostapd_data *global_hapd;
static int close_error,frees,unset,disable_calls,public_disable,owner=WPS_OWNER_REGISTRAR;
static bool bound=true;
static int wps_get_owner(void){return owner;}
static uint32_t esp32qjs_wps_ap_result_identity(const void *p){return bound && p==global_hapd;}
static struct hostapd_data *hostapd_get_hapd_data(void){return global_hapd;}
static int wifi_ap_wps_disable_internal(void){disable_calls++;if(!close_error){owner=WPS_OWNER_NONE;bound=false;}return close_error;}
static int esp_wifi_ap_wps_disable(void){public_disable++;return close_error;}
static int esp_wifi_get_wps_type_internal(void){return WPS_TYPE_DISABLE;}
static int esp_wifi_get_wps_status_internal(void){return WPS_STATUS_DISABLE;}
static int esp_wifi_unset_appie_internal(int id){(void)id;unset++;return 0;}
static void os_free(void *p){if(p==global_hapd)frees++;}
static void wpa_deinit(void *p){(void)p;}
static void forced_memzero(void *p,size_t n){(void)p;(void)n;}
static void hostapd_config_free_bss(void *p){(void)p;}
'''
