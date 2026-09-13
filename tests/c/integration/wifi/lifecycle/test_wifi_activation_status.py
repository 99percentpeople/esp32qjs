"""Real production metadata converter with VM allocation/GC faults; run deferred."""
from tests.support.fixtures import fixture_text
import tempfile
import unittest
from tests.support.wireless_vm_fixture import ROOT, build, extract, run
from tests.c.integration.wifi.config.test_wifi_config_controls import structure

MAIN = fixture_text('wifi/lifecycle/test_wifi_activation_status/main.inc')


class WiFiActivationStatus(unittest.TestCase):
    def test_status_metadata_allocation_failure_and_moving_gc(self):
        header = (ROOT / 'components/esp32_mquickjs/internal/esp32_mquickjs_wifi_radio.h').read_text()
        wifi = (ROOT / 'components/esp32_mquickjs/src/modules/wifi/esp32_mquickjs_wifi.c').read_text()
        body = 'typedef int esp_err_t;\n' + structure(header, 'esp32_mquickjs_wifi_radio_config_result_t')
        body += extract(wifi, 'wifi_make_configuration_status')
        with tempfile.TemporaryDirectory() as tmp:
            binary = build(tmp, body, MAIN)
            run([str(binary)])
