"""Deferred production TWT policy input/readback with real movable MQuickJS GC."""
from tests.support.fixtures import fixture_text
import re
import tempfile
import unittest
from pathlib import Path
from tests.c.integration.wifi.config.test_wifi_config_controls import structure
from tests.c.integration.wifi.driver.test_wifi_driver_phy import COMPONENT
from tests.support.wireless_vm_fixture import CORE, build, extract, run


class WiFiTwtControlsGC(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.temp.cleanup)
        source = (COMPONENT / 'src/modules/wifi_twt/esp32_mquickjs_wifi_twt.c').read_text()
        sdk = Path('/home/zach/esp/esp-idf/components/esp_wifi/include/esp_wifi_he_types.h').read_text()
        options = (CORE / 'esp32_mquickjs_options.c').read_text().replace(
            '#include "esp32_mquickjs_options.h"',
            (COMPONENT / 'internal/esp32_mquickjs_options.h').read_text().replace(
                '#include "esp32_mquickjs_types.h"', ''))
        code = structure(sdk, 'wifi_twt_config_t') + options
        code += re.search(r'^#define SET\(.*$', source, re.M).group(0) + '\n'
        code += extract(source, 'twt_policy_capture') + extract(source, 'twt_policy_value')
        cls.binary = build(cls.temp.name, code, MAIN)

    def test_real_policy_capture_and_result_nth_allocation_gc(self):
        run([str(self.binary)])


MAIN = fixture_text('wifi/twt/test_wifi_twt_controls_gc/main.inc')
