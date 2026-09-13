"""Deferred production Raw TX Radio physical recovery and exact owner retention.

Uses the existing production Radio registry/STOP/shutdown paths. Broker snapshots,
SDK completion/barriers and the rate ledger storage are injected boundaries; real
broker and rate ledger implementations have separate fixtures. No import, compile
or execution while the Wi-Fi API implementation phase remains open.
"""
from tests.support.fixtures import fixture_text
import re
import unittest
from tests.c.integration.wifi.tx.test_wifi_action_recovery import recovery_code as action_code
from tests.c.integration.wifi.driver.test_wifi_driver_phy import COMPONENT
from tests.support.native_compile import compile_run
from tests.support.wireless_vm_fixture import extract
from tests.c.integration.wifi.lifecycle.test_wifi_recovery_runtime import recovery_request_code


def recovery_code(profile, ap):
    source = (COMPONENT / 'src/modules/wifi_radio/esp32_mquickjs_wifi_radio.c').read_text()
    code = action_code(profile, ap)
    code = '#define CONFIG_IDF_TARGET_' + profile.split('/')[0].upper() + ' 1\n' + code
    code = code.replace('struct {unsigned identity,generation;bool restore_pending;} s_tx_rate_lease;',
                        'struct {unsigned identity,generation,write_identity;int interface,previous;bool restore_pending;} s_tx_rate_lease;')
    code = code.replace('static unsigned s_tx_rates,s_policies;', fixture_text('wifi/tx/test_wifi_raw_tx_recovery/recovery_code-code.inc'))
    code = re.sub(r'static struct \{struct \{unsigned generation,identity,radio_lease_identity;\} operation;bool stopped,sdk_fenced;\} s_raw_tx_recovery;',
        'typedef struct {unsigned generation,identity,radio_lease_identity;} esp32_mquickjs_wifi_raw_tx_token_t;\n' +
        re.search(r'static struct \{\n    esp32_mquickjs_wifi_radio_lifecycle_t lifecycle;[^}]*\} s_raw_tx_recovery;', source).group(0), code)
    for name in ('wifi_radio_raw_tx_recovery_exact_locked','wifi_radio_raw_tx_recovery_owner_locked',
                 'esp32_mquickjs_wifi_raw_tx_broker_quiesce'):
        body=extract(code,name)
        code=code.replace(body,body[:body.index('{')].rstrip()+';\n')
    code = code.replace('typedef struct {uint32_t generation;} esp32_mquickjs_wifi_raw_tx_broker_status_t;', fixture_text('wifi/tx/test_wifi_raw_tx_recovery/recovery_code-code-02.inc'))
    code = code.replace('*out=(esp32_mquickjs_wifi_raw_tx_broker_status_t){0};', '*out=raw_native;')
    code = code.replace('assert(locks && !critical && s_action.lease.acquired);', 'assert(locks && !critical && raw_native.token.identity);')
    code = code.replace('assert(s_action.lease.acquired && s_radio.operation.identity);', 'assert(raw_unregistered && s_raw_tx_recovery.sdk_fenced && raw_native.token.identity);')
    body=extract(code,'esp32_mquickjs_wifi_raw_tx_broker_reset_after_deinit')
    code=code.replace(body,fixture_text('wifi/tx/test_wifi_raw_tx_recovery/recovery_code-code-03.inc'))
    code += BOUNDARIES
    for name in ('wifi_radio_raw_tx_unpin','esp32_mquickjs_wifi_radio_raw_tx_retire',
                 'wifi_radio_raw_tx_recovery_exact_locked','wifi_radio_raw_tx_recovery_owner_locked',
                 'esp32_mquickjs_wifi_radio_raw_tx_recovery_active','esp32_mquickjs_wifi_radio_begin_raw_tx_recovery',
                 'wifi_radio_raw_tx_recovery_phase','esp32_mquickjs_wifi_radio_stop_raw_tx_recovery',
                 'esp32_mquickjs_wifi_radio_shutdown_raw_tx_recovery','esp32_mquickjs_wifi_radio_check_stopped_raw_tx_recovery',
                 'esp32_mquickjs_wifi_radio_finish_raw_tx_recovery'):
        code += extract(source,name)
    code += recovery_request_code(ftm=False)
    for name in ('esp32_mquickjs_wifi_radio_begin_recovery','esp32_mquickjs_wifi_radio_recovery_active',
                 'esp32_mquickjs_wifi_radio_stop_recovery','esp32_mquickjs_wifi_radio_shutdown_recovery',
                 'esp32_mquickjs_wifi_radio_check_stopped_recovery','esp32_mquickjs_wifi_radio_finish_recovery'):
        code += extract(source,name)
    return code


class WiFiRawTxRecovery(unittest.TestCase):
    def test_exact_admission_stop_barriers_physical_termination_and_owner_release(self):
        for profile in ('esp32c3/representative','esp32s3/representative-psram','esp32c5/representative'):
            for ap in (False,True):
                with self.subTest(profile=profile,ap=ap):
                    compile_run(self,recovery_code(profile,ap)+MAIN)


BOUNDARIES = fixture_text('wifi/tx/test_wifi_raw_tx_recovery/boundaries.inc')

MAIN = fixture_text('wifi/tx/test_wifi_raw_tx_recovery/main.inc')
