"""Deferred real TWT control dispatch, Radio owner admission and restart policy.

Only SDK commands, native storage and task scheduling are injected. No runtime
fixtures are imported, compiled or executed before Wi-Fi API closure.
"""
import re
import unittest
from pathlib import Path
from test_wifi_driver_phy import COMPONENT
from test_wifi_config_controls import structure
from test_wifi_connection_controls import control_code
from test_wireless_control_regression import compile_run
from wireless_vm_fixture import extract


def control_types():
    sdk = Path('/home/zach/esp/esp-idf/components/esp_wifi/include/esp_wifi_he_types.h').read_text()
    header = (COMPONENT / 'internal/esp32_mquickjs_wifi_twt_controls.h').read_text()
    return structure(sdk, 'wifi_twt_config_t') + re.search(
        r'typedef enum \{[^}]*\} esp32_mquickjs_wifi_twt_control_kind_t;', header).group(0) + structure(
        header, 'esp32_mquickjs_wifi_twt_control_t')


def policy_support(radio):
    return control_types() + re.search(r'static struct \{[^}]*\} s_twt_policy;', radio).group(0) + SDK_BOUNDARY


class WiFiTwtControls(unittest.TestCase):
    def test_native_dispatch_association_range_real_readback_and_discard(self):
        sdk = (COMPONENT / 'src/modules/wifi_twt/esp32_mquickjs_wifi_twt_sdk.c').read_text()
        code = '#include <assert.h>\n#include <stdbool.h>\n#include <stdint.h>\n#include <string.h>\ntypedef int esp_err_t;\n'
        code += control_types() + structure(sdk, 'twt_control_command_t') + NATIVE_BOUNDARY
        code += extract(sdk, 'twt_control_dispatch') + extract(sdk, 'esp32_mquickjs_wifi_twt_sdk_control')
        compile_run(self, code + NATIVE_MAIN)

    def test_actual_radio_exact_helper_and_managed_twt_owners(self):
        radio = (COMPONENT / 'src/modules/wifi_radio/esp32_mquickjs_wifi_radio.c').read_text()
        wifi = (COMPONENT / 'src/modules/wifi/esp32_mquickjs_wifi.c').read_text()
        code = '#define CONFIG_SOC_WIFI_HE_SUPPORT 1\n#define CONFIG_IDF_TARGET_ESP32C5 1\n'
        code += control_code('esp32c5/representative') + policy_support(radio) + OWNER_STORAGE
        for name in ('wifi_radio_twt_individual_lease_retained', 'wifi_radio_twt_broadcast_lease_retained',
                     'esp32_mquickjs_wifi_radio_twt_control'):
            code += extract(radio, name)
        code += extract(wifi, 'esp32_mquickjs_wifi_apply_twt_control')
        compile_run(self, code + RADIO_MAIN)

    def test_actual_restart_policy_snapshot_and_replay(self):
        from test_wifi_restart_configs import config_code, MAIN
        code = config_code('esp32c5/representative', True, mutation_boundary=True)
        code += MAIN[:MAIN.index('static void disabled_pmf_replay')]
        compile_run(self, code + r'''
int main(void){
    setup();wifi_radio_operation_lock();native_twt_policy=(wifi_twt_config_t){true,false};
    assert(wifi_radio_restart_configs_capture_locked(&token,WIFI_MODE_STA)==ESP_OK);
    assert(s_config_restart.snapshot->twt_policy.post_wakeup_event);
    new_driver();assert(!s_twt_policy.known);
    assert(wifi_radio_restart_configs_replay_locked(&token)==ESP_OK);
    assert(wifi_radio_restart_configs_pre_start_locked(&token,WIFI_MODE_STA)==ESP_OK);
    s_radio.started=s_radio.stop_required=native_running=true;
    s_radio.driver_state=ESP32_MQUICKJS_WIFI_RADIO_STARTED;
    assert(wifi_radio_restart_configs_post_start_locked(&token)==ESP_OK);
    assert(native_twt_policy.post_wakeup_event && !native_twt_policy.twt_enable_keep_alive && s_twt_policy.known);
    unsigned before=calls;assert(wifi_radio_restart_configs_post_start_locked(&token)==ESP_OK && calls==before);
    assert(wifi_radio_restart_configs_commit_locked(&token,WIFI_MODE_STA)==ESP_OK);
    wipe();wifi_radio_operation_unlock();return 0;
}
''')


SDK_BOUNDARY = r'''
static wifi_twt_config_t native_twt_policy;
static int esp32_mquickjs_wifi_twt_sdk_control(esp32_mquickjs_wifi_twt_control_kind_t kind,
    esp32_mquickjs_wifi_twt_control_t *value){
    bool write=kind==ESP32_MQUICKJS_WIFI_TWT_WRITE_CONFIG || kind==ESP32_MQUICKJS_WIFI_TWT_WRITE_OFFSET;
    int error=sdk_step(write);if(error)return error;
    if(kind==ESP32_MQUICKJS_WIFI_TWT_WRITE_CONFIG)native_twt_policy=value->config;
    if(kind<=ESP32_MQUICKJS_WIFI_TWT_WRITE_CONFIG)value->config=native_twt_policy;
    if(kind==ESP32_MQUICKJS_WIFI_TWT_READ_FLOWS)value->flow_bitmap=129;
    return ESP_OK;
}
'''

OWNER_STORAGE = r'''
#define ESP_ERR_WIFI_NOT_ASSOC 3015
#define ESP32_MQUICKJS_WIFI_TWT_MAX_INDIVIDUAL 8
typedef struct {struct {unsigned identity;} token;esp32_mquickjs_wifi_radio_lease_t lease;bool native_retired;} wifi_radio_twt_individual_t;
typedef wifi_radio_twt_individual_t wifi_radio_twt_broadcast_t;
static wifi_radio_twt_individual_t individual[8],*s_twt_individual=individual;
static wifi_radio_twt_broadcast_t *broadcast[32],**s_twt_broadcast=broadcast;
'''

RADIO_MAIN = r'''
int main(void){
    reset();esp32_mquickjs_wifi_twt_control_t value={.config={true,false}};uint32_t generation;
    assert(esp32_mquickjs_wifi_apply_twt_control(ESP32_MQUICKJS_WIFI_TWT_WRITE_CONFIG,&value,&generation,&result)==ESP_OK);
    assert(s_twt_policy.known && generation==7 && native_twt_policy.post_wakeup_event);
    s_radio.leases[3]=(wifi_radio_live_lease_t){88,ESP32_MQUICKJS_WIFI_RADIO_CLIENT_TWT};
    unsigned before=calls;
    assert(esp32_mquickjs_wifi_apply_twt_control(ESP32_MQUICKJS_WIFI_TWT_WRITE_CONFIG,&value,&generation,&result)==ESP_ERR_INVALID_STATE && calls==before);
    individual[0].token.identity=99;
    individual[0].lease=(esp32_mquickjs_wifi_radio_lease_t){7,88,ESP32_MQUICKJS_WIFI_RADIO_CLIENT_TWT,true};
    assert(esp32_mquickjs_wifi_apply_twt_control(ESP32_MQUICKJS_WIFI_TWT_WRITE_CONFIG,&value,&generation,&result)==ESP_OK);
    individual[0].lease.generation=6;before=calls;
    assert(esp32_mquickjs_wifi_apply_twt_control(ESP32_MQUICKJS_WIFI_TWT_WRITE_CONFIG,&value,&generation,&result)==ESP_ERR_INVALID_STATE && calls==before);
    reset();s_radio.leases[0]=(wifi_radio_live_lease_t){88,ESP32_MQUICKJS_WIFI_RADIO_CLIENT_CSI};
    s_wifi_application.identity=88;s_wifi_application.acquired=false;before=calls;
    assert(esp32_mquickjs_wifi_apply_twt_control(ESP32_MQUICKJS_WIFI_TWT_WRITE_CONFIG,&value,&generation,&result)==ESP_ERR_INVALID_STATE && calls==before);
    reset();memset(&s_twt_policy,0,sizeof(s_twt_policy));s_wifi_state.radio_lease.identity++;before=calls;
    assert(esp32_mquickjs_wifi_apply_twt_control(ESP32_MQUICKJS_WIFI_TWT_WRITE_OFFSET,&value,&generation,&result)==ESP_ERR_INVALID_STATE && calls==before);
    reset();value.offset_us=102401;
    assert(esp32_mquickjs_wifi_apply_twt_control(ESP32_MQUICKJS_WIFI_TWT_WRITE_OFFSET,&value,&generation,&result)==ESP_ERR_INVALID_ARG && !calls);
    value.offset_us=102400;
    assert(esp32_mquickjs_wifi_apply_twt_control(ESP32_MQUICKJS_WIFI_TWT_WRITE_OFFSET,&value,&generation,&result)==ESP_OK);
    assert(esp32_mquickjs_wifi_radio_twt_control(NULL,NULL,NULL,ESP32_MQUICKJS_WIFI_TWT_READ_FLOWS,&value,&generation,&result)==ESP_OK && value.flow_bitmap==129);
    fail_at=calls+1;
    assert(esp32_mquickjs_wifi_apply_twt_control(ESP32_MQUICKJS_WIFI_TWT_WRITE_CONFIG,&value,&generation,&result)==77 && s_radio.fault_stage);
    return 0;
}
'''

NATIVE_BOUNDARY = r'''
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_INVALID_ARG 1
#define ESP_ERR_INVALID_STATE 2
#define ESP_ERR_INVALID_RESPONSE 3
#define ESP_ERR_WIFI_NOT_ASSOC 4
static uint8_t g_ic[64],g_pm_cfg[128],station_storage[256],node_storage[1100];
static bool in_wifi,discard,corrupt;static unsigned policy_writes,offset_writes;static int bitmap=129,native_error;
static bool current_task_is_wifi_task(void){return in_wifi;}
static int eloop_register_timeout_blocking(int (*fn)(void*,void*),void *a,void *b){
    assert(!in_wifi);if(discard)return -1;in_wifi=true;int result=fn(a,b);in_wifi=false;return result;}
static void pm_twt_set_config(wifi_twt_config_t *config){assert(in_wifi);++policy_writes;
    g_pm_cfg[84]=config->post_wakeup_event;g_pm_cfg[85]=config->twt_enable_keep_alive;if(corrupt)g_pm_cfg[84]^=1;}
static int ieee80211_itwt_get_flow_id_status(int *out){assert(in_wifi);*out=bitmap;return native_error;}
static int wifi_sta_itwt_set_target_wake_time_offset_process(void *message){assert(in_wifi);++offset_writes;
    if(native_error)return native_error;uint32_t offset=((uint32_t*)message)[3];
    if(corrupt)++offset;memcpy(node_storage+1060,&offset,4);return 0;}
static void associate(void){const void *p=station_storage;memcpy(g_ic+16,&p,sizeof(p));p=node_storage;
    memcpy(station_storage+228,&p,sizeof(p));uint32_t state=5;memcpy(station_storage+152,&state,4);}
'''

NATIVE_MAIN = r'''
int main(void){
    esp32_mquickjs_wifi_twt_control_t value={.config={true,false}};
    discard=true;assert(esp32_mquickjs_wifi_twt_sdk_control(ESP32_MQUICKJS_WIFI_TWT_WRITE_CONFIG,&value)==ESP_FAIL && !policy_writes);
    discard=false;assert(esp32_mquickjs_wifi_twt_sdk_control(ESP32_MQUICKJS_WIFI_TWT_WRITE_CONFIG,&value)==ESP_OK && g_pm_cfg[84]==1);
    g_pm_cfg[84]=0;g_pm_cfg[85]=1;
    assert(esp32_mquickjs_wifi_twt_sdk_control(ESP32_MQUICKJS_WIFI_TWT_READ_CONFIG,&value)==ESP_OK && !value.config.post_wakeup_event && value.config.twt_enable_keep_alive);
    assert(esp32_mquickjs_wifi_twt_sdk_control(ESP32_MQUICKJS_WIFI_TWT_READ_FLOWS,&value)==ESP_ERR_WIFI_NOT_ASSOC);
    associate();assert(esp32_mquickjs_wifi_twt_sdk_control(ESP32_MQUICKJS_WIFI_TWT_READ_FLOWS,&value)==ESP_OK && value.flow_bitmap==129);
    bitmap=256;assert(esp32_mquickjs_wifi_twt_sdk_control(ESP32_MQUICKJS_WIFI_TWT_READ_FLOWS,&value)==ESP_ERR_INVALID_RESPONSE && value.flow_bitmap==129);
    value.offset_us=102401;assert(esp32_mquickjs_wifi_twt_sdk_control(ESP32_MQUICKJS_WIFI_TWT_WRITE_OFFSET,&value)==ESP_ERR_INVALID_ARG && !offset_writes);
    value.offset_us=102400;assert(esp32_mquickjs_wifi_twt_sdk_control(ESP32_MQUICKJS_WIFI_TWT_WRITE_OFFSET,&value)==ESP_OK);
    corrupt=true;assert(esp32_mquickjs_wifi_twt_sdk_control(ESP32_MQUICKJS_WIFI_TWT_WRITE_OFFSET,&value)==ESP_ERR_INVALID_RESPONSE);
    native_error=77;assert(esp32_mquickjs_wifi_twt_sdk_control(ESP32_MQUICKJS_WIFI_TWT_WRITE_OFFSET,&value)==77);
    return 0;
}
'''
