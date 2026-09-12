"""Deferred actual HAL adapters with native allocator/storage/error boundaries.

Fixed C5 target compilation checks ROM/ABI; this host unit checks wrapper error
and ownership behavior, not native counter layouts or Wi-Fi task scheduling.
"""
import re
import unittest
from test_wifi_config_controls import structure
from test_wifi_driver_phy import COMPONENT
from test_wireless_control_regression import compile_run
from wireless_vm_fixture import extract


class WiFiHeStatisticsSdk(unittest.TestCase):
    def test_rx_pair_errors_repeat_enable_and_tx_partial_allocations(self):
        source = (COMPONENT / 'src/modules/wifi_radio/esp32_mquickjs_wifi_he_statistics_sdk.c').read_text()
        code = BOUNDARIES + extract(source, '__wrap_hal_enable_rx_statistics')
        code += extract(source, '__wrap_hal_enable_tx_statistics')
        compile_run(self, code + MAIN)

    def test_actual_snapshot_dispatch_retirement_and_discard(self):
        source = (COMPONENT / 'src/modules/wifi_radio/esp32_mquickjs_wifi_he_statistics_sdk.c').read_text()
        header = (COMPONENT / 'internal/esp32_mquickjs_wifi_he_statistics.h').read_text()
        code = BOUNDARIES + extract(source, '__wrap_hal_enable_rx_statistics')
        code += extract(source, '__wrap_hal_enable_tx_statistics')
        code += structure(header, 'esp32_mquickjs_wifi_he_statistics_t')
        code += structure(source, 'he_statistics_command_t')
        code += r'''
#define ESP_FAIL -1
#define ESP_ERR_INVALID_STATE 259
#define ESP_ERR_INVALID_RESPONSE 260
#define esp_test_rx_statistics rx_storage
#define esp_test_rx_error_occurs rx_storage[2]
#define esp_test_rx_mu_statistics multi_user
#define esp_test_tx_statistics_aci_bitmap aci_bitmap
static bool in_wifi_task,discard;
static bool current_task_is_wifi_task(void){return in_wifi_task;}
static int eloop_register_timeout_blocking(int (*handler)(void*,void*),void *a,void *b){
    assert(!in_wifi_task);if(discard)return -1;in_wifi_task=true;
    int result=handler(a,b);in_wifi_task=false;return result;
}
'''
        code += extract(source, 'he_statistics_snapshot_dispatch')
        code += extract(source, 'esp32_mquickjs_wifi_he_statistics_snapshot')
        compile_run(self, code + r'''
int main(void){
    esp32_mquickjs_wifi_he_statistics_t actual;
    assert(__wrap_hal_enable_rx_statistics(true,true)==ESP_OK);
    for(unsigned i=0;i<4;++i)assert(__wrap_hal_enable_tx_statistics(i,true)==ESP_OK);
    assert(live==16);discard=true;
    assert(esp32_mquickjs_wifi_he_statistics_snapshot(&actual,true)==ESP_FAIL && live==16);
    discard=false;
    assert(esp32_mquickjs_wifi_he_statistics_snapshot(&actual,false)==ESP_OK && live==16);
    assert(actual.ordinary && actual.multi_user && actual.tx_mask==15);
    assert(esp32_mquickjs_wifi_he_statistics_snapshot(&actual,true)==ESP_OK && !live);
    assert(actual.ordinary && actual.multi_user && actual.tx_mask==15);
    in_wifi_task=true;
    assert(esp32_mquickjs_wifi_he_statistics_snapshot(&actual,true)==ESP_OK && !live);
    assert(!actual.ordinary && !actual.multi_user && !actual.tx_mask);
    assert(__wrap_hal_enable_rx_statistics(true,false)==ESP_OK);
    void *held=rx_storage[2];rx_storage[2]=NULL;
    assert(esp32_mquickjs_wifi_he_statistics_snapshot(&actual,true)==ESP_ERR_INVALID_RESPONSE && live==3);
    rx_storage[2]=held;assert(esp32_mquickjs_wifi_he_statistics_snapshot(&actual,true)==ESP_OK && !live);
    return 0;
}
''')


BOUNDARIES = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
typedef int esp_err_t;typedef unsigned esp_wifi_aci_t;
#define ESP_WIFI_ACI_MAX 4
#define ESP_OK 0
#define ESP_ERR_INVALID_ARG 258
#define ESP_ERR_NO_MEM 257
static void *rx_storage[3],*multi_user;
static void *esp_test_tx_statistics[4],*esp_test_tx_tb_statistics[4],*esp_test_tx_fail_statistics[4][6];
static unsigned allocations,live,fail_at,mu_calls;static uint8_t aci_bitmap;
static void *native_alloc(void){++allocations;if(allocations==fail_at)return NULL;void *p=malloc(32);assert(p);++live;return p;}
static void native_free(void *p){assert(p && live);--live;free(p);}
static struct {void (*_free)(void *);} g_wifi_osi_funcs={native_free};
static int esp_test_enable_rx_statistics(void){
    for(unsigned i=0;i<3;++i){rx_storage[i]=native_alloc();if(!rx_storage[i])return ESP_ERR_NO_MEM;}return ESP_OK;}
static void esp_test_disable_rx_statistics(void){for(unsigned i=0;i<3;++i)if(rx_storage[i]){native_free(rx_storage[i]);rx_storage[i]=NULL;}}
static int esp_test_enable_rx_mu_statistics(void){++mu_calls;multi_user=native_alloc();return multi_user?ESP_OK:ESP_ERR_NO_MEM;}
static void esp_test_disable_rx_mu_statistics(void){if(multi_user){native_free(multi_user);multi_user=NULL;}}
static void esp_test_disable_tx_statistics(esp_wifi_aci_t aci){
    if(!(aci_bitmap&(1U<<aci)))return; /* Fixed native partial-cleanup defect. */
    if(esp_test_tx_statistics[aci])native_free(esp_test_tx_statistics[aci]);
    if(esp_test_tx_tb_statistics[aci])native_free(esp_test_tx_tb_statistics[aci]);
    if(esp_test_tx_fail_statistics[aci][0])native_free(esp_test_tx_fail_statistics[aci][0]);
    esp_test_tx_statistics[aci]=esp_test_tx_tb_statistics[aci]=esp_test_tx_fail_statistics[aci][0]=NULL;
    aci_bitmap&=~(1U<<aci);
}
static int esp_test_enable_tx_statistics(esp_wifi_aci_t aci){
    if(aci_bitmap&(1U<<aci))return ESP_OK;
    esp_test_tx_statistics[aci]=native_alloc();if(!esp_test_tx_statistics[aci])return ESP_ERR_NO_MEM;
    esp_test_tx_tb_statistics[aci]=native_alloc();if(!esp_test_tx_tb_statistics[aci])return ESP_ERR_NO_MEM;
    esp_test_tx_fail_statistics[aci][0]=native_alloc();if(!esp_test_tx_fail_statistics[aci][0])return ESP_ERR_NO_MEM;
    for(unsigned i=1;i<6;++i)esp_test_tx_fail_statistics[aci][i]=(char*)esp_test_tx_fail_statistics[aci][0]+i;
    aci_bitmap|=1U<<aci;return ESP_OK;
}
'''

MAIN = r'''
int main(void){
    for(unsigned nth=1;nth<=4;++nth){allocations=mu_calls=0;fail_at=nth;
        assert(__wrap_hal_enable_rx_statistics(true,true)==ESP_ERR_NO_MEM && !live);
        assert(mu_calls==(nth==4?1U:0U));}
    fail_at=0;assert(__wrap_hal_enable_rx_statistics(true,true)==ESP_OK && live==4);
    assert(__wrap_hal_enable_rx_statistics(true,true)==ESP_OK && live==4);
    assert(__wrap_hal_enable_rx_statistics(false,true)==ESP_OK && live==1);
    assert(__wrap_hal_enable_rx_statistics(false,false)==ESP_OK && !live);
    for(unsigned aci=0;aci<4;++aci){
        fail_at=0;assert(__wrap_hal_enable_tx_statistics((aci+1)%4,true)==ESP_OK && live==3);
        for(unsigned nth=1;nth<=3;++nth){allocations=0;fail_at=nth;
            assert(__wrap_hal_enable_tx_statistics(aci,true)==ESP_ERR_NO_MEM && live==3);
            assert(!(aci_bitmap&(1U<<aci)) && !esp_test_tx_statistics[aci] && !esp_test_tx_tb_statistics[aci]);
            for(unsigned j=0;j<6;++j)assert(!esp_test_tx_fail_statistics[aci][j]);}
        fail_at=0;assert(__wrap_hal_enable_tx_statistics(aci,true)==ESP_OK && live==6);
        unsigned before=allocations;assert(__wrap_hal_enable_tx_statistics(aci,true)==ESP_OK && allocations==before);
        assert(__wrap_hal_enable_tx_statistics(aci,false)==ESP_OK && live==3);
        assert(__wrap_hal_enable_tx_statistics((aci+1)%4,false)==ESP_OK && !live);
    }
    assert(__wrap_hal_enable_tx_statistics(4,true)==ESP_ERR_INVALID_ARG && !live);
    return 0;
}
'''
