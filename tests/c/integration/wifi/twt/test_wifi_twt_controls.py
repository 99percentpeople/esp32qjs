"""Deferred real TWT control dispatch, Radio owner admission and restart policy.

Only SDK commands, native storage and task scheduling are injected. No runtime
fixtures are imported, compiled or executed before Wi-Fi API closure.
"""
from tests.support.fixtures import fixture_text
import re
import unittest
from pathlib import Path
from tests.c.integration.wifi.driver.test_wifi_driver_phy import COMPONENT
from tests.c.integration.wifi.config.test_wifi_config_controls import structure
from tests.c.integration.wifi.station.test_wifi_connection_controls import control_code
from tests.support.native_compile import compile_run
from tests.support.wireless_vm_fixture import extract


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
        from tests.c.integration.wifi.lifecycle.test_wifi_restart_configs import config_code, MAIN
        code = config_code('esp32c5/representative', True, mutation_boundary=True)
        code += MAIN[:MAIN.index('static void disabled_pmf_replay')]
        compile_run(self, code + fixture_text('wifi/twt/test_wifi_twt_controls/test_actual_restart_policy_snapshot_and_replay.inc'))


SDK_BOUNDARY = fixture_text('wifi/twt/test_wifi_twt_controls/sdk_boundary.inc')

OWNER_STORAGE = fixture_text('wifi/twt/test_wifi_twt_controls/owner_storage.inc')

RADIO_MAIN = fixture_text('wifi/twt/test_wifi_twt_controls/radio_main.inc')

NATIVE_BOUNDARY = fixture_text('wifi/twt/test_wifi_twt_controls/native_boundary.inc')

NATIVE_MAIN = fixture_text('wifi/twt/test_wifi_twt_controls/native_main.inc')
