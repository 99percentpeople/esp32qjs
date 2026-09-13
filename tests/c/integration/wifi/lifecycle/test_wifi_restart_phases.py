"""Deferred production restart phase admission, checkpoint and replay.

Physical shutdown/init and netif ordering are not executed by this fixture.
The rebuild rejection cases install traps at that boundary; the existing SDK
reset fixture supplies a new physical generation for the real replay path.
"""
from tests.support.fixtures import fixture_text
import re
import unittest

from tests.c.integration.wifi.lifecycle.test_wifi_restart_configs import config_code, MAIN as CONFIG_MAIN
from tests.c.integration.wifi.driver.test_wifi_driver_phy import COMPONENT
from tests.c.integration.wifi.monitor.test_wifi_rx_target import unit
from tests.support.native_compile import compile_run
from tests.support.wireless_vm_fixture import extract


def phase_code(profile, ap):
    radio = (COMPONENT / 'src/modules/wifi_radio/esp32_mquickjs_wifi_radio.c').read_text()
    code = config_code(profile, ap)
    policy = unit(COMPONENT / 'internal/esp32_mquickjs_wifi_policy.h')
    policy += unit(COMPONENT / 'src/modules/wifi_radio/esp32_mquickjs_wifi_policy.c')
    policy += '\nstatic esp32_mquickjs_wifi_policy_state_t s_policies;\n'
    policy += re.search(r'static struct \{[^}]*\} s_policy_restart;', radio).group(0)
    code = code.replace('static struct {esp32_mquickjs_wifi_radio_lifecycle_t owner;} s_policy_restart;', policy)
    code = code.replace('static int wifi_radio_shutdown_locked(void) {assert(locks==1);return cleanup_error;}',
                        'static int wifi_radio_shutdown_locked(void) {assert(!"unexpected physical shutdown");return 77;}')
    code += fixture_text('wifi/lifecycle/test_wifi_restart_phases/phase_code.inc')
    for name in ('wifi_radio_policy_restart_prepare_locked', 'wifi_radio_policy_restart_replay_locked',
                 'wifi_radio_check_stopped_lifecycle_locked', 'wifi_radio_restart_checkpoint_matches_locked',
                 'wifi_radio_checkpoint_restart_locked', 'esp32_mquickjs_wifi_radio_checkpoint_restart_lifecycle',
                 'wifi_radio_rebuild_restart_locked', 'esp32_mquickjs_wifi_radio_rebuild_restart_lifecycle',
                 'wifi_radio_replay_restart_locked', 'esp32_mquickjs_wifi_radio_replay_restart_lifecycle'):
        code += extract(radio, name)
    code += CONFIG_MAIN[:CONFIG_MAIN.index('static void disabled_pmf_replay')]
    return code


class WiFiRestartPhases(unittest.TestCase):
    def test_real_phase_admission_frozen_checkpoint_stop_retry_and_replay(self):
        for profile in ('esp32c3/representative', 'esp32s3/representative-psram', 'esp32c5/representative'):
            for ap in (False, True):
                with self.subTest(profile=profile, ap=ap):
                    compile_run(self, phase_code(profile, ap) + MAIN)


MAIN = fixture_text('wifi/lifecycle/test_wifi_restart_phases/main.inc')
