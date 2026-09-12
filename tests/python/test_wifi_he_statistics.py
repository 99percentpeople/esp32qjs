"""Deferred production HE Radio admission, STOP intent and restore suffix tests.

Only SDK calls and scheduler/owner storage are injected. Native allocation and
Wi-Fi-task snapshot behavior are covered separately, not replaced by this fixture.
"""
import re
import unittest
from test_wifi_connection_controls import control_code
from test_wifi_driver_phy import COMPONENT
from test_wifi_config_controls import structure
from test_wireless_control_regression import compile_run
from wireless_vm_fixture import extract


def he_support(radio):
    header = (COMPONENT / 'internal/esp32_mquickjs_wifi_he_statistics.h').read_text()
    sdk = (COMPONENT / 'src/modules/wifi_radio/esp32_mquickjs_wifi_he_statistics_sdk.c').read_text()
    code = '\n#define ESP32_MQUICKJS_WIFI_HE_STATISTICS_AVAILABLE 1\n'
    code += structure(header, 'esp32_mquickjs_wifi_he_statistics_t')
    code += extract(header, 'wifi_he_statistics_equal')
    code += re.search(r'static struct \{[^}]*\} s_he_statistics;', radio).group(0)
    code += BOUNDARIES + extract(sdk, 'esp32_mquickjs_wifi_he_statistics_restore')
    for name in ('wifi_radio_retire_he_statistics_locked', 'wifi_radio_restore_he_statistics_locked'):
        code += extract(radio, name)
    return code


class WiFiHeStatistics(unittest.TestCase):
    def test_exact_owners_failed_write_observation_stop_and_restore(self):
        radio = (COMPONENT / 'src/modules/wifi_radio/esp32_mquickjs_wifi_radio.c').read_text()
        wifi = (COMPONENT / 'src/modules/wifi/esp32_mquickjs_wifi.c').read_text()
        code = control_code('esp32c5/representative') + he_support(radio)
        for name in ('wifi_radio_he_statistics_admission_locked', 'esp32_mquickjs_wifi_radio_read_he_statistics',
                     'esp32_mquickjs_wifi_radio_write_he_statistics'):
            code += extract(radio, name)
        code += extract(wifi, 'esp32_mquickjs_wifi_apply_he_statistics')
        compile_run(self, code + MAIN)

    def test_actual_restart_capture_and_replay(self):
        from test_wifi_restart_configs import config_code, MAIN as CONFIG_MAIN
        code = config_code('esp32c5/representative', True, mutation_boundary=True, he_statistics=True)
        code += CONFIG_MAIN[:CONFIG_MAIN.index('static void disabled_pmf_replay')]
        compile_run(self, code + r'''
int main(void){
    setup();wifi_radio_operation_lock();
    native_statistics=(esp32_mquickjs_wifi_he_statistics_t){true,true,9};
    assert(wifi_radio_restart_configs_capture_locked(&token,WIFI_MODE_STA)==ESP_OK);
    assert(wifi_he_statistics_equal(&s_config_restart.snapshot->he_statistics,&native_statistics));
    assert(wifi_radio_retire_he_statistics_locked()==ESP_OK && !native_statistics.tx_mask);
    new_driver();memset(&s_he_statistics,0,sizeof(s_he_statistics));
    assert(wifi_radio_restart_configs_replay_locked(&token)==ESP_OK);
    assert(wifi_radio_restart_configs_pre_start_locked(&token,WIFI_MODE_STA)==ESP_OK);
    s_radio.started=s_radio.stop_required=native_running=true;
    s_radio.driver_state=ESP32_MQUICKJS_WIFI_RADIO_STARTED;
    assert(wifi_radio_restart_configs_post_start_locked(&token)==ESP_OK);
    assert(native_statistics.ordinary && native_statistics.multi_user && native_statistics.tx_mask==9);
    assert(s_config_restart.he_completed==5);
    unsigned rx_before=rx_writes;
    assert(wifi_radio_restart_configs_post_start_locked(&token)==ESP_OK && rx_writes==rx_before);
    assert(wifi_radio_restart_configs_commit_locked(&token,WIFI_MODE_STA)==ESP_OK);
    wipe();wifi_radio_operation_unlock();
    return 0;
}
''')


BOUNDARIES = r'''
typedef unsigned esp_wifi_aci_t;
static esp32_mquickjs_wifi_he_statistics_t native_statistics;
static unsigned rx_writes,tx_writes[4],retire_calls;
static bool bad_statistics_read;
static int esp_wifi_enable_rx_statistics(bool ordinary,bool mu){
    ++rx_writes;int error=sdk_step(true);
    native_statistics.ordinary=!error && ordinary;native_statistics.multi_user=!error && mu;return error;
}
static int esp_wifi_enable_tx_statistics(esp_wifi_aci_t aci,bool enabled){
    assert(aci<4);++tx_writes[aci];int error=sdk_step(true);
    if(!error){if(enabled)native_statistics.tx_mask|=1U<<aci;else native_statistics.tx_mask&=~(1U<<aci);}return error;
}
static int esp32_mquickjs_wifi_he_statistics_snapshot(esp32_mquickjs_wifi_he_statistics_t *actual,bool retire){
    int error=sdk_step(false);if(error)return error;
    if(bad_statistics_read)return ESP_ERR_INVALID_RESPONSE;
    *actual=native_statistics;
    if(retire){++retire_calls;native_statistics=(esp32_mquickjs_wifi_he_statistics_t){0};}return ESP_OK;
}
'''


MAIN = r'''
static esp32_mquickjs_wifi_he_statistics_t actual;
static void fresh(void){reset();memset(&s_he_statistics,0,sizeof(s_he_statistics));
    native_statistics=(esp32_mquickjs_wifi_he_statistics_t){0};bad_statistics_read=false;
    rx_writes=retire_calls=0;memset(tx_writes,0,sizeof(tx_writes));}
static int change(bool rx,unsigned selection,bool enabled){
    return esp32_mquickjs_wifi_apply_he_statistics(rx,selection,enabled,&actual,&result);}
int main(void){
    fresh();assert(change(true,3,false)==ESP_OK && actual.ordinary && actual.multi_user);
    assert(change(false,2,true)==ESP_OK && actual.tx_mask==4);
    s_wifi_state.status.connected=true;assert(change(false,0,true)==ESP_OK && actual.tx_mask==5);
    unsigned before=calls;s_wifi_state.radio_lease.identity++;
    assert(change(false,0,false)==ESP_ERR_INVALID_STATE && calls==before);
    fresh();s_radio.leases[3]=(wifi_radio_live_lease_t){88,ESP32_MQUICKJS_WIFI_RADIO_CLIENT_CSI};
    assert(change(true,1,false)==ESP_ERR_INVALID_STATE && !calls);
    fresh();s_radio.started=false;assert(change(true,1,false)==ESP_ERR_WIFI_NOT_STARTED && !calls);
    fresh();s_wifi_state.scan_in_progress=true;assert(change(true,1,false)==ESP_ERR_INVALID_STATE && !calls);
    fresh();assert(change(true,3,false)==ESP_OK);fail_at=calls+2;
    assert(change(true,1,false)==77 && !actual.ordinary && !actual.multi_user && !s_radio.fault_stage);
    fresh();bad_statistics_read=true;assert(change(true,1,false)==ESP_ERR_INVALID_RESPONSE && !rx_writes);
    fresh();assert(change(true,3,false)==ESP_OK && change(false,3,true)==ESP_OK);
    wifi_radio_operation_lock();
    assert(wifi_radio_retire_he_statistics_locked()==ESP_OK && retire_calls==1 && !native_statistics.tx_mask);
    assert(s_he_statistics.pending && !s_he_statistics.managed && s_he_statistics.saved.tx_mask==8);
    assert(wifi_radio_retire_he_statistics_locked()==ESP_OK && retire_calls==1);
    s_radio.started=false;assert(wifi_radio_restore_he_statistics_locked()==ESP_ERR_INVALID_STATE);
    s_radio.started=true;before=rx_writes;fail_at=calls+3; /* RX and voice accepted, video fails. */
    assert(wifi_radio_restore_he_statistics_locked()==77 && rx_writes==before+1 && s_he_statistics.completed==2);
    assert(s_he_statistics.pending && s_he_statistics.managed && s_radio.fault_stage);
    before=rx_writes;fail_at=0;s_radio.fault_stage=NULL;
    assert(wifi_radio_restore_he_statistics_locked()==ESP_OK && rx_writes==before);
    assert(!s_he_statistics.pending && native_statistics.tx_mask==8);
    fail_at=calls+1;
    assert(wifi_radio_retire_he_statistics_locked()==77 && s_he_statistics.managed && s_radio.cleanup_stage);
    fail_at=0;
    assert(wifi_radio_retire_he_statistics_locked()==ESP_OK && !s_he_statistics.managed && s_he_statistics.pending);
    wifi_radio_operation_unlock();
    return 0;
}
'''
