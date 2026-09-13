"""Deferred actual CSI Radio read: exact lease, SDK errors and unavailable targets."""
from tests.support.fixtures import fixture_text
import unittest
from tests.c.integration.wifi.station.test_wifi_connection_controls import control_code
from tests.c.integration.wifi.driver.test_wifi_driver_phy import COMPONENT
from tests.support.native_compile import compile_run
from tests.support.wireless_vm_fixture import extract


class WiFiCsiConfigRead(unittest.TestCase):
    def test_exact_identity_serialization_and_sdk_support(self):
        radio = (COMPONENT / 'src/modules/wifi_radio/esp32_mquickjs_wifi_radio.c').read_text()
        for he, profile in ((False, 'esp32c3/representative'), (True, 'esp32c5/representative')):
            code = f'#define CONFIG_SOC_WIFI_HE_SUPPORT {int(he)}\n'
            code += control_code(profile, False, ('wifi_csi_config_t',))
            code += 'static int esp_wifi_get_csi_config(wifi_csi_config_t *p){memset(p,0,sizeof(*p));return sdk_step(false);}\n'
            code += extract(radio, 'esp32_mquickjs_wifi_radio_read_csi_config')
            with self.subTest(profile=profile):
                compile_run(self, code + MAIN)


MAIN = fixture_text('wifi/csi/test_wifi_csi_config_read/main.inc')
