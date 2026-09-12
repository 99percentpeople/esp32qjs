"""Deferred production TX rate ledger and Radio admission tests.

Compiles exact SDK inventory types plus production helpers/wrappers. The fixture
injects only driver calls and locks/state storage. Run in the Wi-Fi stage phase;
this implementation batch performs AST parsing only.
"""
import json
import re
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

from wireless_vm_fixture import extract
from test_wireless_control_regression import compile_run

ROOT = Path(__file__).resolve().parents[2]
COMPONENT = ROOT / 'components/esp32_mquickjs'


def rate_code(profile, predefined=()):
    symbols = json.loads((ROOT / 'docs/idf-wifi-api-inventory.json').read_text())['variants'][profile]['symbols']
    declarations = {}
    for key, entry in symbols.items():
        declaration = entry.get('declaration', '')
        if declaration.startswith('typedef '):
            name = key.split('::')[-1]
            if name not in declarations or '{' in declaration:
                declarations[name] = declaration
    seen, output = set(predefined), []

    def visit(name):
        if name in seen:
            return
        seen.add(name)
        for dependency in sorted(set(re.findall(r'\b\w+_t\b', declarations[name])) - {name}):
            if dependency in declarations:
                visit(dependency)
        output.append(declarations[name] + ';\n')

    for name in ('wifi_interface_t', 'wifi_phy_rate_t', 'wifi_phy_mode_t', 'wifi_tx_rate_config_t'):
        visit(name)
    code = '#define CONFIG_ESP32_MQUICKJS_FEATURE_WIFI 1\n#define CONFIG_ESP_WIFI_SOFTAP_SUPPORT 1\n#define CONFIG_SOC_WIFI_SUPPORTED 1\n'
    if profile.startswith('esp32c5/'):
        code += '#define CONFIG_SOC_WIFI_HE_SUPPORT 1\n#define CONFIG_SOC_WIFI_SUPPORT_5G 1\n'
    code += '#include <stdbool.h>\n#include <stdint.h>\n#include <stddef.h>\n#include <string.h>\n#include <assert.h>\n'
    code += 'typedef int esp_err_t;\n#define ESP_OK 0\n#define ESP_ERR_INVALID_ARG 1\n#define ESP_ERR_INVALID_STATE 2\n#define ESP_ERR_NOT_SUPPORTED 5\n'
    code += ''.join(output)
    for path in ('internal/esp32_mquickjs_wifi_tx_rate.h',
                 'src/modules/wifi_driver/esp32_mquickjs_wifi_tx_rate.c'):
        code += re.sub(r'^#(?:pragma once|include .*).*$', '', (COMPONENT / path).read_text(), flags=re.M)
    return code


class WiFiTxRate(unittest.TestCase):
    def test_records_rollback_fault_admission_repair_and_identity_exhaustion(self):
        compiler = shutil.which('cc')
        if compiler is None:
            self.skipTest('C compiler unavailable')
        radio = (COMPONENT / 'src/modules/wifi_radio/esp32_mquickjs_wifi_radio.c').read_text()
        for profile in ('esp32c3/representative', 'esp32s3/representative-psram', 'esp32c5/representative'):
            with self.subTest(profile=profile), tempfile.TemporaryDirectory() as tmp:
                code = rate_code(profile) + BOUNDARIES
                for name in ('wifi_radio_record_fault', 'wifi_radio_cleanup_fault',
                             'wifi_radio_write_tx_rate', 'esp32_mquickjs_wifi_radio_tx_rate_status',
                             'esp32_mquickjs_wifi_radio_configure_tx_rate'):
                    code += extract(radio, name)
                source, binary = Path(tmp) / 'fixture.c', Path(tmp) / 'fixture'
                source.write_text(code + MAIN)
                built = subprocess.run([compiler, '-std=c11', '-Wall', '-Wextra', '-Werror',
                                        str(source), '-o', str(binary)], capture_output=True, text=True, timeout=30)
                self.assertEqual(built.returncode, 0, built.stderr)
                result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=30)
                self.assertEqual(result.returncode, 0, result.stderr)

    def test_real_restart_capture_replay_suffix_and_identity_budget(self):
        for profile in ('esp32c3/representative', 'esp32s3/representative-psram', 'esp32c5/representative'):
            with self.subTest(profile=profile):
                compile_run(self, rate_code(profile) + REPLAY_MAIN)


BOUNDARIES = r'''
#define WIFI_RADIO_MAX_LEASES 4
#define ESP32_MQUICKJS_WIFI_RADIO_STOPPED 1
#define ESP32_MQUICKJS_WIFI_RADIO_CLEANUP_PENDING 2
#define ESP32_MQUICKJS_WIFI_RADIO_FAULTED 3
static unsigned lock_depth,critical_depth,writes;
static int replies[2];
static wifi_tx_rate_config_t observed[2];
static void wifi_radio_operation_lock(void) { assert(!lock_depth); ++lock_depth; }
static void wifi_radio_operation_unlock(void) { assert(lock_depth==1); --lock_depth; }
#define taskENTER_CRITICAL(lock) do { (void)(lock); assert(!critical_depth); ++critical_depth; } while(0)
#define taskEXIT_CRITICAL(lock) do { (void)(lock); assert(critical_depth==1); --critical_depth; } while(0)
static struct {
    int lock,driver_state;
    bool driver_owned,storage_configured,started,stop_required,restart_required,promiscuous_claimed;
    uint32_t wake_locks,generation;
    struct { uint32_t identity; } lifecycle,operation,leases[WIFI_RADIO_MAX_LEASES];
    const char *fault_stage,*cleanup_stage;
    esp_err_t fault_error,cleanup_error;
} s_radio={.driver_state=ESP32_MQUICKJS_WIFI_RADIO_STOPPED,.driver_owned=true,.storage_configured=true,.generation=7};
static esp32_mquickjs_wifi_tx_rate_state_t s_tx_rates={.next_identity=1};
static const char s_tx_rate_fault[]="tx-rate-uncertain";
static esp32_mquickjs_wifi_tx_rate_lease_t s_tx_rate_lease;
static esp_err_t esp_wifi_config_80211_tx(wifi_interface_t interface,wifi_tx_rate_config_t *config) {
    assert(lock_depth==1 && !critical_depth && writes<2);
    assert(interface==WIFI_IF_STA || interface==WIFI_IF_AP);
    observed[writes]=*config;
    /* Native pointer is temporary: SDK modification cannot corrupt the ledger. */
    config->rate=WIFI_PHY_RATE_1M_L;
    return replies[writes++];
}
'''

MAIN = r'''
#define api(name) esp32_mquickjs_wifi_tx_rate_##name
#define radio(name) esp32_mquickjs_wifi_radio_##name
static wifi_tx_rate_config_t requested={.phymode=WIFI_PHY_MODE_11G,.rate=WIFI_PHY_RATE_6M};
static esp32_mquickjs_wifi_tx_rate_write_t write_result;
static esp32_mquickjs_wifi_tx_rate_record_t record;
static uint32_t generation;
static int apply(int first,int rollback) {
    writes=0;replies[0]=first;replies[1]=rollback;
    return radio(configure_tx_rate)(WIFI_IF_STA,&requested,&write_result,&record,&generation,NULL);
}
int main(void) {
    assert(radio(tx_rate_status)(WIFI_IF_STA,&record,&generation,NULL)==ESP_OK);
    assert(!record.known && !record.uncertain && !record.write_identity && generation==7 && !writes);
    s_radio.leases[2].identity=99;assert(apply(0,0)==ESP_ERR_INVALID_STATE && !writes);
    s_radio.leases[2].identity=0;
    s_radio.started=true;assert(apply(0,0)==ESP_ERR_INVALID_STATE && !writes);s_radio.started=false;
    s_radio.stop_required=true;assert(apply(0,0)==ESP_ERR_INVALID_STATE && !writes);s_radio.stop_required=false;
    s_radio.operation.identity=1;assert(apply(0,0)==ESP_ERR_INVALID_STATE && !writes);s_radio.operation.identity=0;
    s_radio.lifecycle.identity=1;assert(apply(0,0)==ESP_ERR_INVALID_STATE && !writes);s_radio.lifecycle.identity=0;
    s_radio.wake_locks=1;assert(apply(0,0)==ESP_ERR_INVALID_STATE && !writes);s_radio.wake_locks=0;
    s_radio.promiscuous_claimed=true;assert(apply(0,0)==ESP_ERR_INVALID_STATE && !writes);s_radio.promiscuous_claimed=false;
    assert(apply(0,0)==ESP_OK && writes==1 && record.known && record.config.rate==WIFI_PHY_RATE_6M);
    assert(write_result.accepted && record.write_identity==1 && record.generation==7);
    requested.rate=WIFI_PHY_RATE_9M;
    assert(apply(71,0)==71 && writes==2 && write_result.restored && write_result.rollback_attempted);
    assert(record.known && !record.uncertain && record.config.rate==WIFI_PHY_RATE_6M && record.error==71);
    assert(observed[0].rate==WIFI_PHY_RATE_9M && observed[1].rate==WIFI_PHY_RATE_6M && !s_radio.fault_stage);
    assert(apply(72,73)==72 && writes==2 && record.uncertain && !record.known && record.rollback_error==73);
    assert(s_radio.fault_stage==s_tx_rate_fault && s_radio.cleanup_stage==s_tx_rate_fault);
    assert(radio(tx_rate_status)(WIFI_IF_STA,&record,&generation,NULL)==ESP_OK && record.uncertain);
    assert(apply(0,0)==ESP_OK && writes==1 && record.known && !s_radio.fault_stage && !s_radio.cleanup_stage);
    /* A different lifecycle fault cannot be repaired through rate configuration. */
    s_radio.fault_stage="deinit";assert(apply(0,0)==ESP_ERR_INVALID_STATE && !writes);s_radio.fault_stage=NULL;
    s_radio.generation=8;assert(apply(74,0)==74 && writes==1 && !write_result.rollback_attempted && record.uncertain);
    /* Other-interface uncertainty is not cleared by a successful STA repair. */
    s_tx_rates.records[1].uncertain=true;
    assert(apply(0,0)==ESP_OK && record.known && s_radio.fault_stage==s_tx_rate_fault);
    writes=0;replies[0]=0;
    assert(radio(configure_tx_rate)(WIFI_IF_AP,&requested,&write_result,&record,&generation,NULL)==ESP_OK);
    assert(!s_radio.fault_stage && !api(uncertain)(&s_tx_rates));
    uint32_t identity=s_tx_rates.next_identity;
    api(invalidate)(&s_tx_rates);assert(s_tx_rates.next_identity==identity && !s_tx_rates.records[0].known);
    assert(apply(75,0)==75 && writes==1 && !write_result.rollback_attempted);
    assert(apply(0,0)==ESP_OK);
    s_tx_rates.next_identity=UINT32_MAX;assert(apply(0,0)==ESP_OK && record.write_identity==UINT32_MAX);
    assert(!s_tx_rates.next_identity && apply(0,0)==ESP_ERR_INVALID_STATE && !writes && !write_result.attempted);
    wifi_tx_rate_config_t invalid=requested;invalid.phymode=WIFI_PHY_MODE_HT20;
    assert(!api(valid)(&invalid));invalid.rate=WIFI_PHY_RATE_MCS7_SGI;assert(api(valid)(&invalid));
    assert(!strcmp(api(name)(WIFI_PHY_RATE_MCS7_SGI),"mcs7-short"));
    invalid.dcm=true;assert(!api(valid)(&invalid));
#if CONFIG_SOC_WIFI_HE_SUPPORT
    invalid.phymode=WIFI_PHY_MODE_HE20;invalid.rate=WIFI_PHY_RATE_MCS9_SGI;assert(api(valid)(&invalid));
#else
    invalid.phymode=WIFI_PHY_MODE_HE20;assert(!api(valid)(&invalid));
#endif
    assert(!api(name)(-1) && !api(name)(999));
    assert(!lock_depth && !critical_depth);
    return 0;
}
'''


REPLAY_MAIN = r'''
static esp32_mquickjs_wifi_tx_rate_state_t state={.next_identity=1};
static esp32_mquickjs_wifi_tx_rate_snapshot_t snapshot;
static esp32_mquickjs_wifi_tx_rate_write_t output;
static unsigned calls,per_interface[2];static int fail_interface=-1;
static wifi_tx_rate_config_t observed[2];
static int writer(void *opaque,wifi_interface_t iface,const wifi_tx_rate_config_t *config) {
    assert(opaque==&state);++calls;++per_interface[iface];observed[iface]=*config;
    return fail_interface==(int)iface ? 77 : ESP_OK;
}
#define CAPTURE(gen) esp32_mquickjs_wifi_tx_rate_restart_capture(&state,gen,&snapshot)
#define REPLAY(gen) esp32_mquickjs_wifi_tx_rate_replay(&state,gen,&snapshot,&completed,writer,&state,&output)
int main(void) {
    uint8_t completed=0;
    assert(CAPTURE(7)==ESP_OK && snapshot.mask==0 && REPLAY(8)==ESP_OK && !calls);
    wifi_tx_rate_config_t wanted={.phymode=WIFI_PHY_MODE_11G,.rate=WIFI_PHY_RATE_6M};
    for(unsigned i=0;i<2;++i)assert(esp32_mquickjs_wifi_tx_rate_apply(&state,7,i,&wanted,writer,&state,&output)==ESP_OK);
    assert(CAPTURE(8)==ESP_ERR_INVALID_STATE && !snapshot.generation);
    assert(CAPTURE(7)==ESP_OK && snapshot.mask==3);
    esp32_mquickjs_wifi_tx_rate_snapshot_t frozen=snapshot;
    unsigned before=calls;assert(REPLAY(7)==ESP_ERR_INVALID_STATE && calls==before);
    esp32_mquickjs_wifi_tx_rate_invalidate(&state);fail_interface=1;
    assert(REPLAY(8)==77 && completed==1 && state.records[0].known && state.records[1].uncertain);
    assert(snapshot.generation==7 && snapshot.configs[0].rate==WIFI_PHY_RATE_6M && snapshot.configs[1].rate==WIFI_PHY_RATE_6M);
    unsigned station_writes=per_interface[0];fail_interface=-1;
    assert(REPLAY(8)==ESP_OK && completed==3 && per_interface[0]==station_writes);
    before=calls;assert(REPLAY(8)==ESP_OK && calls==before);
    esp32_mquickjs_wifi_tx_rate_invalidate(&state);completed=0;
    assert(REPLAY(9)==ESP_OK && completed==3 && state.records[0].generation==9 && state.records[1].generation==9);
    state.records[0].uncertain=true;
    assert(CAPTURE(9)==ESP_ERR_INVALID_STATE && !snapshot.generation);state.records[0].uncertain=false;
    state.next_identity=UINT32_MAX;assert(CAPTURE(9)==ESP_ERR_INVALID_STATE && !snapshot.generation);
    state.next_identity=UINT32_MAX-1;assert(CAPTURE(9)==ESP_OK);
    esp32_mquickjs_wifi_tx_rate_invalidate(&state);completed=0;
    assert(REPLAY(10)==ESP_OK && completed==3 && state.next_identity==0);
    assert(CAPTURE(10)==ESP_ERR_INVALID_STATE && !snapshot.generation);
    snapshot=frozen;before=calls;completed=0;
    assert(!esp32_mquickjs_wifi_tx_rate_replay_capacity(&state,&snapshot));
    assert(REPLAY(11)==ESP_ERR_INVALID_STATE && calls==before && !output.attempted);
    return 0;
}
'''
