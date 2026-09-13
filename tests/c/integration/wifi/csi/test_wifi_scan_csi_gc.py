"""Deferred actual scan input/result and CSI native result under moving GC/OOM."""
from tests.support.fixtures import fixture_text
import re
import tempfile
import unittest
from tests.c.integration.wifi.config.test_wifi_config_controls import sdk_types
from tests.c.integration.wifi.driver.test_wifi_driver_phy import COMPONENT
from tests.support.wireless_vm_fixture import CORE, build, extract, run


class WiFiScanCsiGC(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.temp.cleanup)
        driver = (COMPONENT / 'src/modules/wifi_driver/esp32_mquickjs_wifi_driver.c').read_text()
        csi = (COMPONENT / 'src/modules/wifi_csi/esp32_mquickjs_wifi_csi.c').read_text()
        options = (CORE / 'esp32_mquickjs_options.c').read_text().replace(
            '#include "esp32_mquickjs_options.h"',
            (COMPONENT / 'internal/esp32_mquickjs_options.h').read_text().replace(
                '#include "esp32_mquickjs_types.h"', ''))
        scan = (COMPONENT / 'internal/esp32_mquickjs_wifi_scan_parameters.h').read_text()
        scan = re.sub(r'^#(?:include|pragma).*$', '', scan, flags=re.M)
        cls.binaries = []
        for he, profile in ((False, 'esp32c3/representative'), (True, 'esp32c5/representative')):
            body = f'#define CONFIG_SOC_WIFI_HE_SUPPORT {int(he)}\n#define CONFIG_SOC_WIFI_MAC_VERSION_NUM {3 if he else 1}\n'
            body += sdk_types(profile, ('wifi_scan_default_params_t', 'wifi_csi_config_t'))
            body += BOUNDARIES + scan + options
            body += extract(driver, 'driver_scan_parameters_capture') + extract(driver, 'driver_scan_parameters_to_js')
            body += extract(csi, 'wifi_csi_native_config_to_js')
            cls.binaries.append(build(cls.temp.name + '/' + str(he), body, MAIN))

    def test_real_capture_converters_getters_nth_allocation_and_gc(self):
        for binary in self.binaries:
            run([str(binary)])


BOUNDARIES = fixture_text('wifi/csi/test_wifi_scan_csi_gc/boundaries.inc')

MAIN = fixture_text('wifi/csi/test_wifi_scan_csi_gc/main.inc')
