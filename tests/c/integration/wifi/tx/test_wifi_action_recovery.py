"""Deferred production Action/ROC physical recovery phases and lease handoff.

The real Radio registry, admission, stop/shutdown, Action ledger and retirement
are composed unchanged. SDK stop/deinit, callback draining, unrelated brokers and
helper/configuration storage are injected boundaries. No fixture import/compile/
execution during API implementation; native SDK and helper integration remain
separate stage gates.
"""
from tests.support.fixtures import fixture_text
import unittest
from tests.c.integration.wifi.tx.test_wifi_action_radio import radio_code, MAIN as RADIO_MAIN
from tests.c.integration.wifi.driver.test_wifi_driver_phy import COMPONENT
from tests.support.native_compile import compile_run
from tests.support.wireless_vm_fixture import extract


def recovery_code(profile, ap):
    source = (COMPONENT / 'src/modules/wifi_radio/esp32_mquickjs_wifi_radio.c').read_text()
    code = radio_code(profile, ap)
    code = code.replace('bool uncertain;} s_interval;', 'bool uncertain,restore_pending;} s_interval;')
    code = code.replace('struct {unsigned identity,generation;} s_tx_rate_lease;',
                        'struct {unsigned identity,generation;bool restore_pending;} s_tx_rate_lease;')
    code = code.replace('bool event_fence_posted,event_fence_seen;', fixture_text('wifi/tx/test_wifi_action_recovery/recovery_code-code.inc'))
    code += BOUNDARIES
    for name in ('wifi_radio_record_fault', 'wifi_radio_cleanup_fault',
                 'wifi_radio_action_recovery_exact_locked',
                 'esp32_mquickjs_wifi_radio_action_recovery_active',
                 'esp32_mquickjs_wifi_radio_check_stopped_action_recovery',
                 'wifi_radio_action_recovery_capture_owner_locked', 'esp32_mquickjs_wifi_radio_begin_action_recovery',
                 'wifi_radio_stop_owners_locked', 'wifi_radio_stop_lease_locked', 'wifi_radio_stop_locked',
                 'wifi_radio_shutdown_lease_locked', 'wifi_radio_shutdown_locked',
                 'wifi_radio_action_recovery_phase', 'esp32_mquickjs_wifi_radio_stop_action_recovery',
                 'esp32_mquickjs_wifi_radio_shutdown_action_recovery',
                 'esp32_mquickjs_wifi_radio_finish_action_recovery',
                 'esp32_mquickjs_wifi_radio_finish_lifecycle'):
        code += extract(source, name)
    return code + RADIO_MAIN[:RADIO_MAIN.index('int main(void)')]


class WiFiActionRecovery(unittest.TestCase):
    def test_exact_admission_physical_proof_owner_consumption_and_cleanup_suffix(self):
        for profile in ('esp32c3/representative', 'esp32s3/representative-psram', 'esp32c5/representative'):
            for ap in (False, True):
                with self.subTest(profile=profile, softap=ap):
                    compile_run(self, recovery_code(profile, ap) + MAIN)


BOUNDARIES = fixture_text('wifi/tx/test_wifi_action_recovery/boundaries.inc')

MAIN = fixture_text('wifi/tx/test_wifi_action_recovery/main.inc')
