"""Deferred production PHY observations: admission, partial reads and VM GC/OOM.

SDK storage and lock boundaries are injected; no RF or GPIO routing is simulated.
These fixtures are written for the collective Wi-Fi stage, not executed yet.
"""
from tests.support.fixtures import fixture_text
import re
import tempfile
import unittest

from tests.c.integration.wifi.config.test_wifi_config_controls import sdk_types
from tests.c.integration.wifi.driver.test_wifi_driver_phy import COMPONENT
from tests.support.native_compile import compile_run
from tests.support.wireless_vm_fixture import CORE, build, extract, run


def antenna_code(profile):
    header = (COMPONENT / 'internal/esp32_mquickjs_wifi_radio.h').read_text()
    radio = (COMPONENT / 'src/modules/wifi_radio/esp32_mquickjs_wifi_radio.c').read_text()
    code = '#include <stdbool.h>\n#include <stdint.h>\n#include <string.h>\n#include <assert.h>\n'
    code += sdk_types(profile, ('esp_phy_ant_config_t', 'esp_phy_ant_gpio_config_t'))
    for kind, name in (('enum', 'esp32_mquickjs_wifi_radio_driver_state_t'),
                       ('union', 'esp32_mquickjs_wifi_antenna_snapshot_t')):
        code += re.search(r'typedef ' + kind + r' \{[^}]*\} ' + name + ';', header).group(0)
    return code + BOUNDARIES + extract(radio, 'esp32_mquickjs_wifi_radio_read_antenna') + RESET


class WiFiDriverAntenna(unittest.TestCase):
    def test_production_admission_partial_reads_full_width_and_decode(self):
        for profile in ('esp32c3/representative', 'esp32s3/representative-psram',
                        'esp32c5/representative'):
            with self.subTest(profile=profile):
                compile_run(self, antenna_code(profile) + MAIN)

    def test_public_fields_errors_arity_and_each_vm_allocation(self):
        code = antenna_code('esp32c5/representative')
        driver = (COMPONENT / 'src/modules/wifi_driver/esp32_mquickjs_wifi_driver.c').read_text()
        code += extract((CORE / 'esp32_mquickjs.c').read_text(), 'esp32_mquickjs_throw_native_error')
        for name in ('driver_read_error', 'driver_antenna_to_js', 'driver_read_antenna',
                     'js_wifi_driver_get_antenna', 'js_wifi_driver_get_antenna_gpio'):
            code += extract(driver, name)
        with tempfile.TemporaryDirectory() as tmp:
            binary = build(tmp, code, VM_MAIN)
            for query in range(2):
                for scenario in ('success', 'sdk-error', 'arity', 'admission', 'decode'):
                    if query == 1 and scenario == 'decode':
                        continue  # All GPIO bit-field representations are retained.
                    run([str(binary), str(query), scenario])


BOUNDARIES = fixture_text('wifi/driver/test_wifi_driver_antenna/boundaries.inc')

RESET = fixture_text('wifi/driver/test_wifi_driver_antenna/reset.inc')

MAIN = fixture_text('wifi/driver/test_wifi_driver_antenna/main.inc')

VM_MAIN = fixture_text('wifi/driver/test_wifi_driver_antenna/vm_main.inc')
