"""Actual Station extension parser + MQuickJS; deferred Wi-Fi phase execution."""
from tests.support.fixtures import fixture_text
import tempfile
import unittest
from tests.support.wireless_vm_fixture import ROOT, CORE, build, extract, run


SDK = fixture_text('wifi/station/test_wifi_station_rssi_preference/sdk.inc')

MAIN = fixture_text('wifi/station/test_wifi_station_rssi_preference/main.inc')


class WiFiStationRssiPreference(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.temp.cleanup)
        options = (CORE / 'esp32_mquickjs_options.c').read_text()
        helpers = '\n'.join(extract(options, name) for name in (
            'esp32_mquickjs_value_to_bounded_u32', 'esp32_mquickjs_value_to_enum'))
        source = (ROOT / 'components/esp32_mquickjs/src/modules/wifi/esp32_mquickjs_wifi_future.c').read_text()
        body = '#include <math.h>\n' + helpers + SDK + ''.join(extract(source, name) for name in (
            'wifi_string_equals', 'wifi_parse_station_phy', 'wifi_parse_station_extensions'))
        cls.binaries = [build(cls.temp.name + '/' + str(gate),
            '#define CONFIG_SOC_WIFI_SUPPORT_5G ' + str(gate) + '\n' + body, MAIN) for gate in (0, 1)]

    def test_boundaries_and_property_gc_allocation_failures(self):
        for value in ('0', '1', '255'):
            run([str(self.binaries[1]), '({rssi5gAdjustment:' + value + '})', '1', value])
        for value in ('-1', '256', '1.5', 'NaN', 'Infinity', 'true', '"4"', 'null', '{}'):
            with self.subTest(value=value):
                run([str(self.binaries[1]), '({rssi5gAdjustment:' + value + '})', '0', '0'])

    def test_unavailable_target_rejects_even_explicit_zero(self):
        for value in ('0', '1', '255'):
            run([str(self.binaries[0]), '({rssi5gAdjustment:' + value + '})', '0', '-1'])
        for binary in self.binaries:
            run([str(binary), '({})', '1', '0'])
