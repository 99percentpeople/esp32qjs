"""Deferred production probe PM ownership, including retained-timer stop.

No import/compile/execution until the Wi-Fi stage. SDK PM calls and locks are
controlled; the ownership helper is the production code used by six call sites.
"""
import unittest
from test_wifi_driver_phy import COMPONENT
from test_wifi_rx_target import unit
from test_wireless_control_regression import compile_run


class WiFiTwtProbeWake(unittest.TestCase):
    def test_repeated_release_does_not_consume_other_pm_owners(self):
        code = PRELUDE
        code += unit(COMPONENT / 'internal/esp32_mquickjs_wifi_twt_probe_wake.h')
        code += unit(COMPONENT / 'src/modules/wifi_twt/esp32_mquickjs_wifi_twt_probe_wake.c')
        compile_run(self, code + MAIN)


PRELUDE = r'''
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#define CONFIG_ESP32_MQUICKJS_FEATURE_WIFI 1
#define CONFIG_SOC_WIFI_HE_SUPPORT 1
#define CONFIG_IDF_TARGET_ESP32C5 1
#define DRAM_ATTR
#define ESP_OK 0
#define ESP_ERR_INVALID_STATE 0x103
typedef int esp_err_t;
typedef unsigned portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
static unsigned locked;
#define portENTER_CRITICAL_SAFE(lock) do {assert(!locked);locked=1;} while(0)
#define portEXIT_CRITICAL_SAFE(lock) do {assert(locked==1);locked=0;} while(0)
'''
MAIN = r'''
static unsigned shared_references=3,ups,downs;
static bool release_inside_up,acquire_inside_down;
static esp32_mquickjs_wifi_twt_probe_wake_snapshot_t snapshot(void) {
    esp32_mquickjs_wifi_twt_probe_wake_snapshot_t s;esp32_mquickjs_wifi_twt_probe_wake_snapshot(&s);return s;
}
void pm_wake_up(void) {
    assert(!locked && snapshot().held && snapshot().acquiring);++ups;++shared_references;
    if(release_inside_up){release_inside_up=false;esp32_mquickjs_wifi_twt_probe_wake_done_native();}
}
void pm_wake_done(void) {
    assert(!locked && !snapshot().held && snapshot().releasing && shared_references>3);++downs;--shared_references;
    if(acquire_inside_down){acquire_inside_down=false;esp32_mquickjs_wifi_twt_probe_wake_up_native();}
}
static void cold_boot(void) {
    memset(&s_probe_wake,0,sizeof(s_probe_wake));shared_references=3;ups=downs=0;
    release_inside_up=acquire_inside_down=false;
}
int main(void) {
    esp32_mquickjs_wifi_twt_probe_wake_done_native();assert(shared_references==3 && downs==0);
    esp32_mquickjs_wifi_twt_probe_wake_up_native();assert(snapshot().held && shared_references==4 && ups==1);
    esp32_mquickjs_wifi_twt_probe_wake_done_native();assert(!snapshot().held && shared_references==3 && downs==1);
    /* A retained timer's later SDK stop releases no additional PM owner. */
    esp32_mquickjs_wifi_twt_probe_wake_done_native();assert(shared_references==3 && downs==1 && snapshot().fault==0);
    for(unsigned i=0;i<100;++i){esp32_mquickjs_wifi_twt_probe_wake_up_native();esp32_mquickjs_wifi_twt_probe_wake_done_native();}
    assert(shared_references==3 && ups==101 && downs==101 && !snapshot().held);
    cold_boot();esp32_mquickjs_wifi_twt_probe_wake_up_native();esp32_mquickjs_wifi_twt_probe_wake_up_native();
    assert(snapshot().fault==ESP_ERR_INVALID_STATE && snapshot().held && ups==1);
    esp32_mquickjs_wifi_twt_probe_wake_done_native();assert(downs==0 && shared_references==4); /* Quarantine, not false drain. */
    cold_boot();release_inside_up=true;esp32_mquickjs_wifi_twt_probe_wake_up_native();
    assert(snapshot().fault==ESP_ERR_INVALID_STATE && snapshot().held && shared_references==4 && downs==0);
    cold_boot();esp32_mquickjs_wifi_twt_probe_wake_up_native();acquire_inside_down=true;
    esp32_mquickjs_wifi_twt_probe_wake_done_native();assert(snapshot().fault==ESP_ERR_INVALID_STATE && ups==1 && shared_references==3);
    return 0;
}
'''
