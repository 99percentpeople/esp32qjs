"""Deferred production EAP snapshot; native task/getter boundaries injected.

Only AST-parse during the Wi-Fi implementation wave. Runtime execution belongs
to the concentrated stage and does not establish EAP server or RF acceptance.
"""
import unittest
from test_wifi_rx_target import unit, INTERNAL
from test_wireless_control_regression import compile_run
from wireless_vm_fixture import ROOT


class WiFiEapSdkSnapshot(unittest.TestCase):
    def test_actual_getter_and_failed_dispatch_do_not_invent_time_policy(self):
        folder = ROOT / 'components/esp32_mquickjs/src/modules/wifi_enterprise'
        code = PRELUDE + unit(INTERNAL / 'esp32_mquickjs_wifi_eap_sdk.h')
        code += unit(folder / 'esp32_mquickjs_wifi_eap_sdk.c')
        compile_run(self, code + MAIN)


PRELUDE = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#define CONFIG_ESP32_MQUICKJS_FEATURE_WIFI 1
#define CONFIG_ESP_WIFI_ENTERPRISE_SUPPORT 1
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_INVALID_ARG 2
#define ESP_ERR_INVALID_STATE 3
typedef int esp_err_t;
static bool on_wifi,dispatch_failure,time_disabled;
static int getter_error;
static unsigned calls,dispatches;
bool current_task_is_wifi_task(void){return on_wifi;}
unsigned esp32qjs_eap_native_resources(void){assert(on_wifi);return 13;}
int esp32qjs_eap_native_cleanup_error(void){assert(on_wifi);return 71;}
int esp32qjs_eap_native_control_error(void){assert(on_wifi);return 72;}
esp_err_t esp_eap_client_get_disable_time_check(bool *out){
    assert(on_wifi && out);++calls;*out=time_disabled;return getter_error;
}
static int eloop_register_timeout_blocking(int (*fn)(void *,void *),void *a,void *b){
    assert(!on_wifi);++dispatches;if(dispatch_failure)return ESP_FAIL;
    on_wifi=true;int result=fn(a,b);on_wifi=false;return result;
}
'''


MAIN = r'''
int main(void){
    /* New policy flags occupy existing padding, preserving this ABI/budget. */
    assert(sizeof(esp32_mquickjs_wifi_eap_sdk_snapshot_t)==16);
    esp32_mquickjs_wifi_eap_sdk_snapshot_t out;
    assert(esp32_mquickjs_wifi_eap_sdk_snapshot(NULL)==ESP_ERR_INVALID_ARG);
    assert(!calls && !dispatches);
    assert(esp32_mquickjs_wifi_eap_sdk_snapshot(&out)==ESP_OK);
    assert(out.entered && out.time_check_known && !out.disable_time_check);
    assert(out.resources==13 && out.cleanup_error==71 && out.control_error==72);
    time_disabled=true;on_wifi=true;
    assert(esp32_mquickjs_wifi_eap_sdk_snapshot(&out)==ESP_OK);
    assert(out.time_check_known && out.disable_time_check && dispatches==1);
    getter_error=73;
    assert(esp32_mquickjs_wifi_eap_sdk_snapshot(&out)==ESP_OK);
    assert(out.entered && !out.time_check_known && out.resources==13);
    on_wifi=false;dispatch_failure=true;
    assert(esp32_mquickjs_wifi_eap_sdk_snapshot(&out)==ESP_FAIL);
    assert(!out.entered && !out.time_check_known && !out.disable_time_check);
    assert(out.resources==UINT32_MAX && calls==3);
    return 0;
}
'''
