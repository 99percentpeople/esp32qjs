"""Deferred real storage/RSSI/event-mask writes, STOP history, admission and config replay.

SDK, physical generation and lock boundaries are injected. The mutation aliases,
Radio setters, STOP predicate, owner registry admission and configuration replay
are production code. No RF, NVS or physical-lifecycle proof is inferred.
"""
from tests.support.fixtures import fixture_text
import unittest

from tests.c.integration.wifi.lifecycle.test_wifi_restart_configs import config_code, MAIN as CONFIG_MAIN
from tests.c.integration.wifi.driver.test_wifi_driver_phy import COMPONENT
from tests.support.native_compile import compile_run
from tests.support.wireless_vm_fixture import extract
from tests.c.integration.wifi.config.test_wifi_config_controls import structure


def stopped_controls_code(profile, ap=True, reviewed=True):
    radio = (COMPONENT / 'src/modules/wifi_radio/esp32_mquickjs_wifi_radio.c').read_text()
    code = config_code(profile, ap, mutation_boundary=True)
    header = (COMPONENT / 'internal/esp32_mquickjs_wifi_radio.h').read_text()
    code += structure(header, 'esp32_mquickjs_wifi_radio_restart_selection_t')
    code += '#define ESP32_MQUICKJS_WIFI_POLICY_SLOT_AP_11B 2\nstatic struct {struct {bool configured;} records[4];} s_policies;\n'
    code = code.replace('struct {unsigned identity,generation;} operation,lifecycle;',
                        'struct {unsigned identity,generation;} operation;'
                        'esp32_mquickjs_wifi_radio_lifecycle_t lifecycle;uint32_t next_lifecycle_identity;')
    if not reviewed:
        code = code.replace(f'#define CONFIG_IDF_TARGET_{profile.split("/")[0].upper()} 1', '')
    # Move this production definition after the real SDK aliases. The shared
    # control fixture normally isolates it from the Radio-local macro boundary.
    control = extract(code, 'esp32_mquickjs_wifi_radio_connection_control')
    code = code.replace(control, control[:control.index('\n{')] + ';\n')
    code += control
    code += ''.join(extract(radio, name) for name in (
        'wifi_radio_begin_lifecycle_with_dependents_locked', 'wifi_radio_begin_lifecycle_locked', 'esp32_mquickjs_wifi_radio_begin_stopped_restart',
        'esp32_mquickjs_wifi_radio_set_storage', 'esp32_mquickjs_wifi_radio_event_mask'))
    code += CONFIG_MAIN[:CONFIG_MAIN.index('static void disabled_pmf_replay')]
    return code + HELPERS


class WiFiStoppedControls(unittest.TestCase):
    def test_reviewed_writes_keep_history_and_capture_new_storage(self):
        for profile in ('esp32c3/representative', 'esp32s3/representative-psram', 'esp32c5/representative'):
            for ap in (False, True):
                with self.subTest(profile=profile, softap=ap):
                    compile_run(self, stopped_controls_code(profile, ap) + MAIN)

    def test_mask_capture_rollback_and_fault_admission(self):
        for profile in ('esp32c3/representative', 'esp32s3/representative-psram', 'esp32c5/representative'):
            for ap in (False, True):
                with self.subTest(profile=profile, softap=ap):
                    compile_run(self, stopped_controls_code(profile, ap) + MASK_MAIN)

    def test_unreviewed_target_keeps_writer_invalidation(self):
        compile_run(self, stopped_controls_code('esp32c5/representative', reviewed=False) + FALLBACK_MAIN)


HELPERS = fixture_text('wifi/lifecycle/test_wifi_stopped_controls/helpers.inc')


MAIN = fixture_text('wifi/lifecycle/test_wifi_stopped_controls/main.inc')


FALLBACK_MAIN = fixture_text('wifi/lifecycle/test_wifi_stopped_controls/fallback_main.inc')


MASK_MAIN = fixture_text('wifi/lifecycle/test_wifi_stopped_controls/mask_main.inc')
