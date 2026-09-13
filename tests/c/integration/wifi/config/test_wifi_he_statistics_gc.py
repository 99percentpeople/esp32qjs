"""Deferred real MQuickJS HE option capture and nested native readback under GC/OOM."""
from tests.support.fixtures import fixture_text
import re
import tempfile
import unittest
from tests.c.integration.wifi.config.test_wifi_config_controls import structure
from tests.c.integration.wifi.driver.test_wifi_driver_phy import COMPONENT
from tests.support.wireless_vm_fixture import CORE, build, extract, run


class WiFiHeStatisticsGC(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.temp.cleanup)
        driver = (COMPONENT / 'src/modules/wifi_driver/esp32_mquickjs_wifi_driver.c').read_text()
        header = (COMPONENT / 'internal/esp32_mquickjs_wifi_he_statistics.h').read_text()
        options = (CORE / 'esp32_mquickjs_options.c').read_text().replace(
            '#include "esp32_mquickjs_options.h"',
            (COMPONENT / 'internal/esp32_mquickjs_options.h').read_text().replace(
                '#include "esp32_mquickjs_types.h"', ''))
        code = structure(header, 'esp32_mquickjs_wifi_he_statistics_t') + BOUNDARIES + options
        code += re.search(r'static const char \*const driver_statistics_categories\[\][^;]*;', driver).group(0)
        for name in ('driver_rx_statistics_capture', 'driver_rx_statistics_to_js', 'js_wifi_driver_get_statistics_config'):
            code += extract(driver, name)
        cls.binary = build(cls.temp.name, code, MAIN)

    def test_capture_and_native_result_with_moving_gc_and_nth_allocation_failure(self):
        run([str(self.binary)])


BOUNDARIES = fixture_text('wifi/config/test_wifi_he_statistics_gc/boundaries.inc')

MAIN = fixture_text('wifi/config/test_wifi_he_statistics_gc/main.inc')
