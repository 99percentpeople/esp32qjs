"""Deferred exact-owner production connection controls and runtime handoff.

Only native state storage, SDK and locks are injected. Does not simulate or prove
RF disconnect/deauth, event delivery/rearming, or physical recovery.
"""
import re
import unittest
from test_wifi_config_controls import sdk_types, structure
from test_wifi_driver_phy import COMPONENT
from test_wireless_control_regression import compile_run
from wireless_vm_fixture import extract


def control_code(profile, ap=True, sdk_extra=()):
    header = (COMPONENT / 'internal/esp32_mquickjs_wifi_radio.h').read_text()
    radio = (COMPONENT / 'src/modules/wifi_radio/esp32_mquickjs_wifi_radio.c').read_text()
    wifi = (COMPONENT / 'src/modules/wifi/esp32_mquickjs_wifi.c').read_text()
    ap_source = (COMPONENT / 'src/modules/wifi/esp32_mquickjs_wifi_ap.c').read_text()
    code = '#include <stdbool.h>\n#include <stdint.h>\n#include <string.h>\n#include <assert.h>\n'
    code += '#define CONFIG_ESP32_MQUICKJS_FEATURE_WIFI 1\n'
    code += f'#define CONFIG_ESP_WIFI_SOFTAP_SUPPORT {int(ap)}\n' + sdk_types(profile, sdk_extra)
    for name in ('esp32_mquickjs_wifi_radio_client_t', 'esp32_mquickjs_wifi_radio_driver_state_t',
                 'esp32_mquickjs_wifi_connection_control_t'):
        code += re.search(r'typedef enum \{[^}]*\} ' + name + ';', header).group(0)
    code += 'typedef int esp_err_t;\n'
    for name in ('esp32_mquickjs_wifi_radio_lease_t', 'esp32_mquickjs_wifi_radio_lifecycle_t',
                 'esp32_mquickjs_wifi_radio_config_result_t', 'esp32_mquickjs_wifi_rssi_request_t'):
        code += structure(header, name)
    code += BOUNDARIES
    code += 'static esp32_mquickjs_wifi_rssi_request_t s_rssi_request;\n'
    code += re.search(r'static struct \{[^}]*\} s_inactive_history;', radio).group(0)
    code += extract(radio, 'wifi_radio_inactive_history_record')
    for name in ('wifi_radio_lease_valid', 'wifi_radio_record_fault', 'wifi_radio_cleanup_fault',
                 'wifi_radio_connection_owner_locked', 'esp32_mquickjs_wifi_radio_rssi_request_status',
                 'esp32_mquickjs_wifi_radio_connection_control'):
        code += extract(radio, name)
    code += extract(wifi, 'esp32_mquickjs_wifi_connection_reserved_locked')
    code += extract(wifi, 'wifi_helpers_idle') + extract(ap_source, 'esp32_mquickjs_wifi_ap_control_lease')
    code += extract(wifi, 'wifi_driver_helpers_ready')
    code += extract(wifi, 'esp32_mquickjs_wifi_apply_connection_control') + RESET
    return code


class WiFiConnectionControls(unittest.TestCase):
    def test_sdk_suffix_failures_exact_identity_handoff_and_rssi_single_write(self):
        for profile in ('esp32c3/representative', 'esp32s3/representative-psram', 'esp32c5/representative'):
            for ap in (False, True):
                with self.subTest(profile=profile, ap=ap):
                    compile_run(self, control_code(profile, ap) + MAIN)


BOUNDARIES = r'''
#define ESP_OK 0
#define ESP_ERR_INVALID_ARG 1
#define ESP_ERR_INVALID_STATE 2
#define ESP_ERR_NOT_SUPPORTED 3
#define ESP_ERR_WIFI_NOT_INIT 4
#define ESP_ERR_WIFI_NOT_STARTED 5
#define ESP_ERR_INVALID_RESPONSE 6
#define ESP_ERR_NO_MEM 17
#define WIFI_RADIO_MAX_LEASES 6
static unsigned locks,critical,helper_locks,calls,writes,fail_at,rollback_fail;
static uint16_t inactive[2]={6,300};
static int threshold=-60;
static bool corrupt_readback;
typedef struct {uint32_t identity;esp32_mquickjs_wifi_radio_client_t client;} wifi_radio_live_lease_t;
static struct {
    int lock;uint32_t generation,wake_locks;
    bool driver_owned,storage_configured,started,promiscuous_claimed,restart_required;
    struct {unsigned identity;} operation,lifecycle;
    const char *fault_stage,*cleanup_stage;esp_err_t fault_error,cleanup_error;
    esp32_mquickjs_wifi_radio_driver_state_t driver_state;
    wifi_mode_t effective_mode;
    wifi_storage_t storage;
    wifi_radio_live_lease_t leases[WIFI_RADIO_MAX_LEASES];
    esp32_mquickjs_wifi_radio_config_result_t configuration;
} s_radio;
static struct {unsigned identity;} s_tx_rate_lease;
static esp32_mquickjs_wifi_radio_lease_t s_wifi_application,s_ap_lease;
static esp32_mquickjs_wifi_radio_lifecycle_t s_wifi_lifecycle;
static bool s_ap_cleanup_pending,s_wifi_configuration_cleanup,s_wifi_ap_stop_cleanup;
static struct {
    esp32_mquickjs_wifi_radio_lease_t radio_lease;
    bool runtime_cleanup_pending;
    const char *cleanup_stage;
    struct {bool connected;} status;
    bool connect_in_progress,connect_draining,connect_start_active,disconnect_active;
    bool scan_in_progress,scan_draining,scan_results_pending,scan_start_active,scan_stop_active;
    bool connect_future_registered,scan_future_registered;
} s_wifi_state;
static esp32_mquickjs_wifi_radio_config_result_t result;
static void wifi_radio_operation_lock(void) {assert(!locks && !helper_locks && !critical);++locks;}
static void wifi_radio_operation_unlock(void) {assert(locks==1 && !critical);--locks;}
static void wifi_lock(void) {assert(!locks && !helper_locks);++helper_locks;}
static void wifi_unlock(void) {assert(helper_locks==1);--helper_locks;}
#define taskENTER_CRITICAL(p) do {(void)(p);assert(!critical);++critical;} while(0)
#define taskEXIT_CRITICAL(p) do {(void)(p);assert(critical==1);--critical;} while(0)
static int sdk_step(bool write) {
    assert(locks==1 && !critical && !helper_locks);++calls;writes+=write;
    if(calls==fail_at)return 77;
    if(result.rollback_attempted && rollback_fail && --rollback_fail==0)return 88;
    return ESP_OK;
}
static int esp_wifi_get_inactive_time(wifi_interface_t iface,uint16_t *value)
{
    *value=inactive[iface];
    if(corrupt_readback && !strcmp(result.stage,"inactive-time-readback") && !result.rollback_attempted)*value+=1;
    return sdk_step(false);
}
static int esp_wifi_set_inactive_time(wifi_interface_t iface,uint16_t value)
{
    inactive[iface]=value;return sdk_step(true);
}
static int esp_wifi_set_rssi_threshold(int32_t value) {threshold=value;return sdk_step(true);}
'''

RESET = r'''
static void reset(void) {
    assert(!locks && !critical && !helper_locks);
    memset(&s_radio,0,sizeof(s_radio));memset(&s_wifi_state,0,sizeof(s_wifi_state));memset(&s_ap_lease,0,sizeof(s_ap_lease));
    memset(&s_wifi_lifecycle,0,sizeof(s_wifi_lifecycle));memset(&result,0,sizeof(result));
    memset(&s_inactive_history,0,sizeof(s_inactive_history));
    memset(&s_rssi_request,0,sizeof(s_rssi_request));
    s_wifi_configuration_cleanup=s_wifi_ap_stop_cleanup=s_ap_cleanup_pending=corrupt_readback=false;
    s_tx_rate_lease.identity=0;s_radio.generation=7;
    s_radio.driver_owned=s_radio.storage_configured=s_radio.started=true;
    s_radio.storage=WIFI_STORAGE_RAM;
    s_radio.driver_state=ESP32_MQUICKJS_WIFI_RADIO_STARTED;s_radio.effective_mode=WIFI_MODE_STA;
    s_wifi_application=(esp32_mquickjs_wifi_radio_lease_t){7,11,ESP32_MQUICKJS_WIFI_RADIO_CLIENT_APPLICATION,true};
    s_wifi_state.radio_lease=(esp32_mquickjs_wifi_radio_lease_t){7,12,ESP32_MQUICKJS_WIFI_RADIO_CLIENT_WIFI_STA,true};
    s_radio.leases[0]=(wifi_radio_live_lease_t){11,s_wifi_application.client};
    s_radio.leases[1]=(wifi_radio_live_lease_t){12,s_wifi_state.radio_lease.client};
#if CONFIG_ESP_WIFI_SOFTAP_SUPPORT
    s_ap_lease=(esp32_mquickjs_wifi_radio_lease_t){7,13,ESP32_MQUICKJS_WIFI_RADIO_CLIENT_WIFI_AP,true};
    s_radio.leases[2]=(wifi_radio_live_lease_t){13,s_ap_lease.client};s_radio.effective_mode=WIFI_MODE_APSTA;
#endif
    inactive[0]=6;inactive[1]=300;threshold=-60;calls=writes=fail_at=rollback_fail=0;
}
'''

MAIN = r'''
static int32_t actual;
static int change(void) {return esp32_mquickjs_wifi_apply_connection_control(ESP32_MQUICKJS_WIFI_CONNECTION_INACTIVE_TIME,WIFI_IF_STA,9,&actual,&result);}
static int rearm(void) {return esp32_mquickjs_wifi_apply_connection_control(ESP32_MQUICKJS_WIFI_CONNECTION_RSSI_THRESHOLD,WIFI_IF_STA,-75,&actual,&result);}
int main(void) {
    reset();assert(change()==ESP_OK && actual==9 && inactive[0]==9 && inactive[1]==300);unsigned total=calls;
    assert(s_radio.leases[0].identity==11 && s_radio.leases[1].identity==12);
    assert(!result.persistent_mutation_possible);
    assert(s_inactive_history.generation==7 && s_inactive_history.known==1 && s_inactive_history.values[0]==9);
    reset();s_radio.storage=WIFI_STORAGE_FLASH;
    assert(change()==ESP_OK && result.persistent_mutation_possible);
    assert(s_radio.configuration.persistent_mutation_possible);
    for(unsigned nth=1;nth<=total;++nth) {
        reset();s_radio.storage=WIFI_STORAGE_FLASH;fail_at=nth;
        assert(change()==77 && result.error==77);
        assert(result.persistent_mutation_possible==result.mutation_attempted);
        assert(s_radio.configuration.persistent_mutation_possible==result.mutation_attempted);
        if(result.mutation_attempted)assert(result.rollback_complete);
    }
    reset();s_radio.storage=WIFI_STORAGE_FLASH;fail_at=total;rollback_fail=1;
    assert(change()==77 && result.rollback_error==88 && result.persistent_mutation_possible);
    reset();s_radio.storage=WIFI_STORAGE_FLASH;
    assert(rearm()==ESP_OK && !result.persistent_mutation_possible);
    for(unsigned nth=1;nth<=total;++nth) {
        reset();fail_at=nth;assert(change()==77 && !actual && result.error==77);
        assert(!locks && !critical && !helper_locks && inactive[0]==6 && inactive[1]==300);
        if(result.mutation_attempted) {
            assert(result.rollback_complete && !s_radio.fault_stage);
            assert(s_inactive_history.known==1 && !s_inactive_history.failed && s_inactive_history.values[0]==6);
        } else {
            assert(!writes && !result.rollback_attempted);
            assert(!s_inactive_history.known && s_inactive_history.failed==1 && s_inactive_history.error==77);
        }
    }
    reset();fail_at=total;assert(change()==77);unsigned suffix=calls-total;
    for(unsigned nth=1;nth<=suffix;++nth) {
        reset();fail_at=total;rollback_fail=nth;assert(change()==77 && result.rollback_error==88);
        assert(!result.rollback_complete && s_radio.fault_error==77 && s_radio.cleanup_error==88);
        assert(!s_inactive_history.known && s_inactive_history.failed==1 && s_inactive_history.error==88);
        unsigned before=calls;fail_at=0;assert(change()==ESP_ERR_INVALID_STATE && calls==before);
    }
    reset();corrupt_readback=true;assert(change()==ESP_ERR_INVALID_RESPONSE && result.rollback_complete && inactive[0]==6);
    reset();s_wifi_state.radio_lease.generation++;assert(change()==ESP_ERR_INVALID_STATE && !calls);
    reset();s_wifi_state.radio_lease.identity++;assert(change()==ESP_ERR_INVALID_STATE && !calls);
    reset();s_wifi_application.client=ESP32_MQUICKJS_WIFI_RADIO_CLIENT_WIFI_STA;assert(change()==ESP_ERR_INVALID_STATE && !calls);
    reset();s_wifi_state.radio_lease.acquired=false;assert(change()==ESP_ERR_INVALID_STATE && !calls);
    reset();s_wifi_state.scan_draining=true;assert(change()==ESP_ERR_INVALID_STATE && !calls && !strcmp(result.stage,"helper-admission"));
    reset();s_wifi_state.runtime_cleanup_pending=true;assert(change()==ESP_ERR_INVALID_STATE && !calls);
    reset();s_radio.leases[3]=(wifi_radio_live_lease_t){21,ESP32_MQUICKJS_WIFI_RADIO_CLIENT_RAW_TX};
    assert(change()==ESP_ERR_INVALID_STATE && !calls);
    assert(rearm()==ESP_OK && actual==-75 && threshold==-75 && calls==1 && writes==1);
    assert(rearm()==ESP_OK && calls==2); /* Same value is an explicit rearm, never deduplicated. */
    reset();fail_at=1;assert(rearm()==77 && threshold==-75 && result.mutation_attempted && !result.rollback_attempted);
    assert(calls==1 && !s_radio.fault_stage && !actual);
    reset();s_radio.wake_locks=1;assert(change()==ESP_ERR_INVALID_STATE && !calls);assert(rearm()==ESP_OK);
    reset();s_radio.started=false;s_radio.driver_state=ESP32_MQUICKJS_WIFI_RADIO_STOPPED;
    memset(s_radio.leases,0,sizeof(s_radio.leases));s_wifi_application.acquired=false;s_wifi_state.radio_lease.acquired=false;s_ap_lease.acquired=false;
    assert(change()==ESP_ERR_WIFI_NOT_STARTED && !calls);assert(rearm()==ESP_OK);
    reset();assert(esp32_mquickjs_wifi_apply_connection_control(ESP32_MQUICKJS_WIFI_CONNECTION_INACTIVE_TIME,WIFI_IF_STA,2,&actual,&result)==ESP_ERR_INVALID_ARG && !calls);
    assert(esp32_mquickjs_wifi_apply_connection_control(ESP32_MQUICKJS_WIFI_CONNECTION_RSSI_THRESHOLD,WIFI_IF_STA,-101,&actual,&result)==ESP_ERR_INVALID_ARG && !calls);
#if CONFIG_ESP_WIFI_SOFTAP_SUPPORT
    reset();s_ap_cleanup_pending=true;assert(change()==ESP_ERR_INVALID_STATE && !calls);
    reset();assert(esp32_mquickjs_wifi_apply_connection_control(ESP32_MQUICKJS_WIFI_CONNECTION_INACTIVE_TIME,WIFI_IF_AP,10,&actual,&result)==ESP_OK);
    assert(inactive[0]==6 && inactive[1]==10);
    reset();s_radio.storage=WIFI_STORAGE_FLASH;
    assert(esp32_mquickjs_wifi_apply_connection_control(ESP32_MQUICKJS_WIFI_CONNECTION_INACTIVE_TIME,WIFI_IF_AP,10,&actual,&result)==ESP_OK);
    assert(result.persistent_mutation_possible && inactive[0]==6 && inactive[1]==10);
#endif
    return 0;
}
'''
