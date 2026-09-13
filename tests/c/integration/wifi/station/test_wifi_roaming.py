"""Deferred production BTM capture/encoder/Radio/supplicant boundary regressions.

Only SDK calls, scheduling and storage are injected. Do not run until the Wi-Fi
phase gate; compilation/execution is intentionally deferred with other fixtures.
"""
from tests.support.fixtures import fixture_text
import re
import tempfile
import unittest
from tests.c.integration.wifi.config.test_wifi_vendor_ie import vendor_code, COMPONENT
from tests.c.integration.wifi.config.test_wifi_config_controls import structure, sdk_types
from tests.c.integration.wifi.monitor.test_wifi_rx_target import unit
from tests.support.native_compile import compile_run
from tests.support.wireless_vm_fixture import CORE, build, extract, run

MODULE = COMPONENT / 'src/modules/wifi_roaming'
HEADER = COMPONENT / 'internal/esp32_mquickjs_wifi_roaming.h'


def declarations():
    source = HEADER.read_text()
    return '\n'.join(re.findall(r'^#define ESP32_MQUICKJS_WIFI_BTM_.*$', source, re.M)) + '\n' + ''.join(
        structure(source, name) for name in ['esp32_mquickjs_wifi_btm_candidate_t',
            'esp32_mquickjs_wifi_btm_query_t', 'esp32_mquickjs_wifi_roaming_result_t'])


class WiFiRoaming(unittest.TestCase):
    def test_actual_radio_admission_and_sdk_dispatch(self):
        radio = (COMPONENT / 'src/modules/wifi_radio/esp32_mquickjs_wifi_radio.c').read_text()
        code = vendor_code('esp32c5/representative') + declarations()
        before = sdk_types('esp32c5/representative')
        with_ap = sdk_types('esp32c5/representative', ('wifi_ap_record_t',))
        assert with_ap.startswith(before)
        code += with_ap[len(before):]
        code += '\n#define CONFIG_ESP_WIFI_RRM_SUPPORT 1\n#define CONFIG_ESP_WIFI_WNM_SUPPORT 1\n#define ESP_FAIL -9\n#define ESP_ERR_WIFI_NOT_STARTED -10\n'
        code += SDK_BOUNDARIES
        code += extract((CORE / 'esp32_mquickjs_wireless_core.c').read_text(), 'esp32_mquickjs_wireless_secure_zero')
        code += unit(MODULE / 'esp32_mquickjs_wifi_roaming_sdk.c')
        code += extract(radio, 'wifi_radio_connection_owner_locked')
        code += extract(radio, 'esp32_mquickjs_wifi_radio_roaming')
        compile_run(self, code + NATIVE_MAIN)

    def test_capture_and_pre_submit_gc_oom(self):
        code = fixture_text('wifi/station/test_wifi_roaming/test_capture_and_pre_submit_gc_oom-code.inc')
        code += 'typedef int esp_err_t;\n#define ESP_OK 0\n' + declarations()
        code += extract((MODULE / 'esp32_mquickjs_wifi_roaming_sdk.c').read_text(), 'esp32_mquickjs_wifi_btm_encode')
        code += VM_BOUNDARY
        code += extract((CORE / 'esp32_mquickjs.c').read_text(), 'esp32_mquickjs_throw_native_error')
        options = (CORE / 'esp32_mquickjs_options.c').read_text()
        header = (COMPONENT / 'internal/esp32_mquickjs_options.h').read_text().replace('#include "esp32_mquickjs_types.h"', '')
        code += options.replace('#include "esp32_mquickjs_options.h"', header)
        code += unit(MODULE / 'esp32_mquickjs_wifi_roaming.c')
        base = '{reason:"rssi",candidates:[{bssid:"02:00:00:00:00:01",bssidInformation:4294967295,operatingClass:81,channel:6,phyType:7,preference:0}]}'
        cases = [(base, True), ('{}', True), ('null', False),
                 (base.replace('"rssi"', '"rssi\\x00"'), False),
                 (base.replace('4294967295', '4294967296'), False),
                 (base.replace('channel:6', 'channel:6.5'), False),
                 (base.replace('02:00', '03:00'), False),
                 (base.replace('preference:0', 'preference:256'), False),
                 (base.replace('reason:', 'extra:1,reason:'), False),
                 (base.replace('bssid:', 'extra:1,bssid:'), False),
                 ('{candidates:new Array(17)}', False), ('{candidates:[undefined]}', False),
                 ('{allowApChannelChange:1}', False),
                 ('{get reason(){gc();return "delay";},get candidates(){gc();return [];}}', True),
                 ('{get reason(){throw 12345;}}', False)]
        with tempfile.TemporaryDirectory() as tmp:
            binary = build(tmp, code, VM_MAIN)
            for expression, valid in cases:
                run([str(binary), "(" + expression + ")", str(int(valid)), 'send'])
            for scenario in ['capabilities', 'rrm', 'btm', 'sdk-error']:
                run([str(binary), "(" + base + ")", '0' if scenario == 'sdk-error' else '1', scenario])


SDK_BOUNDARIES = fixture_text('wifi/station/test_wifi_roaming/sdk_boundaries.inc')
NATIVE_MAIN = fixture_text('wifi/station/test_wifi_roaming/native_main.inc')
VM_BOUNDARY = fixture_text('wifi/station/test_wifi_roaming/vm_boundary.inc')
VM_MAIN = fixture_text('wifi/station/test_wifi_roaming/vm_main.inc')
