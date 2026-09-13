"""Deferred production TWT recovery admission/checkpoint/phase routing.

Uses actual Radio lease registry, TWT owners and close functions. SDK cancellation,
configuration snapshots and physical stop/shutdown are injected call boundaries;
real STOP/deinit/event scheduling and runtime replay remain separate stage gates.
Do not import, compile or execute during Wi-Fi API implementation.
"""
from tests.support.fixtures import fixture_text
import re
import unittest
from tests.c.integration.wifi.twt.test_wifi_twt_agreement_radio import agreement_radio_code, MAIN as RADIO_MAIN
from tests.c.integration.wifi.twt.test_wifi_twt_radio import SOURCE, INTERNAL
from tests.c.integration.wifi.config.test_wifi_config_controls import structure
from tests.support.native_compile import compile_run
from tests.support.wireless_vm_fixture import extract


def recovery_code():
    source = SOURCE.read_text()
    code = agreement_radio_code()
    code = code.replace('bool uncertain;} s_interval;', 'bool uncertain,restore_pending;} s_interval;')
    code = code.replace('struct {unsigned identity,generation;} s_tx_rate_lease;',
                        'struct {unsigned identity,generation;bool restore_pending;} s_tx_rate_lease;')
    code = code.replace('uint32_t next_operation_identity;', 'uint32_t next_operation_identity;bool stop_submitted;unsigned event_phase;')
    header = (INTERNAL / 'esp32_mquickjs_wifi_radio.h').read_text()
    code += re.search(r'typedef enum \{[^}]*\} esp32_mquickjs_wifi_recovery_kind_t;', header).group(0)
    code += structure(header, 'esp32_mquickjs_wifi_recovery_request_t')
    code += re.search(r'static struct \{[^}]*\} s_twt_recovery;', source).group(0)
    code += BOUNDARIES
    for name in ('wifi_radio_twt_recovery_exact_locked', 'wifi_radio_twt_managed_lease_locked',
                 'wifi_radio_twt_recovery_owners_locked', 'wifi_radio_twt_recovery_active',
                 'wifi_radio_twt_recovery_begin', 'wifi_radio_twt_recovery_prepare',
                 'wifi_radio_twt_recovery_checkpoint', 'wifi_radio_twt_recovery_phase',
                 'wifi_radio_twt_recovery_stopped', 'wifi_radio_twt_recovery_finish'):
        code += extract(source, name)
    return code + RADIO_MAIN[:RADIO_MAIN.index('int main(void)')]


class WiFiTwtRecovery(unittest.TestCase):
    def test_foreign_owner_stale_generation_group_freeze_and_original_owner_retirement(self):
        compile_run(self, recovery_code() + MAIN)


BOUNDARIES = fixture_text('wifi/twt/test_wifi_twt_recovery/boundaries.inc')

MAIN = fixture_text('wifi/twt/test_wifi_twt_recovery/main.inc')
