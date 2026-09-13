"""Deferred actual frozen-policy capture/replay, not complete restart/RF proof."""
from tests.support.fixtures import fixture_text
import re
import unittest
from tests.c.integration.wifi.config.test_wifi_policy_record import PRELUDE
from tests.c.integration.wifi.driver.test_wifi_driver_policy import policy_code
from tests.c.integration.wifi.driver.test_wifi_driver_phy import COMPONENT
from tests.c.integration.wifi.monitor.test_wifi_rx_target import unit
from tests.support.native_compile import compile_run
from tests.support.wireless_vm_fixture import extract


class WiFiPolicyReplay(unittest.TestCase):
    def test_real_snapshot_phase_order_suffix_failure_false_values_and_revision_space(self):
        code = PRELUDE + unit(COMPONENT / 'internal/esp32_mquickjs_wifi_policy.h')
        code += unit(COMPONENT / 'src/modules/wifi_radio/esp32_mquickjs_wifi_policy.c')
        compile_run(self, code + CORE_MAIN)

    def test_real_radio_plan_exact_lifecycle_native_phase_rebuild_and_cleanup(self):
        code = policy_code('esp32c5/representative', True, True)
        code = code.replace('struct {unsigned identity;} operation,lifecycle;',
                            'struct {unsigned identity,generation;} operation,lifecycle;')
        radio = (COMPONENT / 'src/modules/wifi_radio/esp32_mquickjs_wifi_radio.c').read_text()
        code += re.search(r'static struct \{[^}]*\} s_policy_restart;', radio).group(0)
        code += CLEANUP_BOUNDARIES
        for name in ('wifi_radio_policy_restart_prepare_locked', 'wifi_radio_policy_restart_replay_locked',
                     'esp32_mquickjs_wifi_radio_finish_lifecycle'):
            code += extract(radio, name)
        compile_run(self, code + RADIO_MAIN)

    def test_production_resume_does_not_publish_owner_before_post_start_acceptance(self):
        compile_run(self, resume_code() + RESUME_MAIN)

    def test_enterprise_install_precedes_snapshot_discard_and_owner_publication(self):
        compile_run(self, resume_code(enterprise=True) + ENTERPRISE_RESUME_MAIN)


def resume_code(enterprise=False):
    code = policy_code('esp32c5/representative', True, True)
    code = code.replace('struct {unsigned identity;} operation,lifecycle;',
                        'struct {unsigned identity,generation;} operation,lifecycle;')
    code = code.replace('int lock;uint32_t generation,wake_locks;',
                        'int lock;uint32_t generation,wake_locks,next_lease_identity;')
    code = code.replace('static bool native_dynamic',
                        'static esp32_mquickjs_wifi_radio_lease_t *observed_owner;\nstatic bool native_dynamic')
    code = code.replace('native_dynamic=value;return sdk_step(true);',
                        'assert(!observed_owner || !observed_owner->acquired);native_dynamic=value;return sdk_step(true);')
    radio = (COMPONENT / 'src/modules/wifi_radio/esp32_mquickjs_wifi_radio.c').read_text()
    code += re.search(r'static struct \{[^}]*\} s_policy_restart;', radio).group(0)
    code += 'struct esp32_mquickjs_wifi_eap_profile;\n'
    code += RESUME_BOUNDARIES
    if enterprise:
        code += ENTERPRISE_RESUME_BOUNDARIES
    for name in ('wifi_radio_policy_restart_prepare_locked', 'wifi_radio_policy_restart_replay_locked',
                 'wifi_radio_resume_lifecycle_locked'):
        code += extract(radio, name)
    return code


CORE_MAIN = fixture_text('wifi/config/test_wifi_policy_replay/core_main.inc')

CLEANUP_BOUNDARIES = fixture_text('wifi/config/test_wifi_policy_replay/cleanup_boundaries.inc')

RADIO_MAIN = fixture_text('wifi/config/test_wifi_policy_replay/radio_main.inc')


RESUME_BOUNDARIES = fixture_text('wifi/config/test_wifi_policy_replay/resume_boundaries.inc')

RESUME_MAIN = fixture_text('wifi/config/test_wifi_policy_replay/resume_main.inc')


ENTERPRISE_RESUME_BOUNDARIES = fixture_text('wifi/config/test_wifi_policy_replay/enterprise_resume_boundaries.inc')

ENTERPRISE_RESUME_MAIN = fixture_text('wifi/config/test_wifi_policy_replay/enterprise_resume_main.inc')
