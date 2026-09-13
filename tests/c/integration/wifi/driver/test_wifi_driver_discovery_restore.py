"""Deferred production restore admission/error and Driver VM conversion cases.

Only SDK calls and native snapshot providers are injected. These cases do not
prove the binary SDK loader, flash durability or RF behavior.
"""
from tests.support.fixtures import fixture_text
import re
import tempfile
import unittest

from tests.c.integration.wifi.config.test_wifi_config_controls import PRELUDE, sdk_types, structure
from tests.support.native_compile import compile_run
from tests.support.wireless_vm_fixture import ROOT, build, extract, run

COMPONENT = ROOT / 'components/esp32_mquickjs'
RADIO = COMPONENT / 'src/modules/wifi_radio/esp32_mquickjs_wifi_radio.c'
HEADER = COMPONENT / 'internal/esp32_mquickjs_wifi_radio.h'
WIFI = COMPONENT / 'src/modules/wifi/esp32_mquickjs_wifi.c'
CAPABILITIES = COMPONENT / 'src/modules/wifi/esp32_mquickjs_wifi_capabilities.c'

NATIVE = fixture_text('wifi/driver/test_wifi_driver_discovery_restore/native.inc')

NATIVE_MAIN = fixture_text('wifi/driver/test_wifi_driver_discovery_restore/native_main.inc')

VM_MAIN = fixture_text('wifi/driver/test_wifi_driver_discovery_restore/vm_main.inc')


class WifiDriverDiscoveryRestore(unittest.TestCase):
    def test_production_restore_admission_and_sdk_failure(self):
        radio, header = RADIO.read_text(), HEADER.read_text()
        declarations = '#undef ESP32_MQUICKJS_WIFI_RADIO_STOPPED\n'
        declarations += re.search(r'typedef enum \{[^}]*\} esp32_mquickjs_wifi_radio_driver_state_t;', header).group(0)
        declarations += re.search(r'typedef enum \{[^}]*\} wifi_radio_restore_phase_t;', radio).group(0)
        declarations += structure(header, 'esp32_mquickjs_wifi_radio_config_result_t')
        functions = ''.join(extract(radio, name) for name in (
            'wifi_radio_record_fault', 'wifi_radio_cleanup_fault',
            'wifi_radio_restore_phase_locked', 'esp32_mquickjs_wifi_radio_restore'))
        for target in ('esp32c3/representative', 'esp32s3/representative-psram', 'esp32c5/representative'):
            for softap in (0, 1):
                with self.subTest(target=target, softap=softap):
                    gates = f'\n#define CONFIG_ESP_WIFI_SOFTAP_SUPPORT {softap}\n'
                    compile_run(self, PRELUDE + gates + sdk_types(target) + declarations + NATIVE + functions + NATIVE_MAIN)

    def test_production_capabilities_gc_and_nth_allocation(self):
        source = CAPABILITIES.read_text()
        table = re.search(r'static const struct \{[^}]*\}\s*wifi_driver_operations\[\] = \{.*?\n\};', source, re.S).group(0)
        constants = fixture_text('wifi/driver/test_wifi_driver_discovery_restore/test_production_capabilities_gc_and_nth_allocation-constants.inc')
        constants += 'static const char *esp_get_idf_version(void){return "fixture-idf";}\n'
        bodies = table + '\n' + '\n'.join(extract(source, name) for name in (
            'wifi_capability_strings', 'wifi_station_capabilities', 'wifi_ap_capabilities',
            'wifi_driver_capability_list', 'js_wifi_driver_capabilities'))
        keys = (COMPONENT / 'internal/esp32_mquickjs_wifi_config_fields.h').read_text()
        for enabled in (0, 1):
            with tempfile.TemporaryDirectory() as directory:
                gates = f'\n#define SECRET_READ {enabled}\n#define CONFIG_ESP32_MQUICKJS_WIFI_ALLOW_SECRET_READBACK {enabled}\n#define CONFIG_ESP_WIFI_SOFTAP_SUPPORT {enabled}\n#define CONFIG_ESP_COEX_POWER_MANAGEMENT {enabled}\n'
                binary = build(directory, constants + gates + keys + bodies, VM_MAIN)
                for gc in (0, 1):
                    run([str(binary), str(gc)])
