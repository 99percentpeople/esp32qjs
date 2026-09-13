"""Deferred production SSID delivery and connect result with SDK sampling boundary."""
from tests.support.fixtures import fixture_text
import tempfile
import unittest
from tests.support.wireless_vm_fixture import ROOT, build, extract, run

BOUNDARIES = fixture_text('wifi/config/test_wifi_ssid_result/boundaries.inc')

MAIN = fixture_text('wifi/config/test_wifi_ssid_result/main.inc')


class WiFiSsidResult(unittest.TestCase):
    def test_binary_span_text_nullable_owned_bytes_and_failure_roots(self):
        base = ROOT / 'components/esp32_mquickjs/src/modules/wifi'
        config = (base / 'esp32_mquickjs_wifi_config.c').read_text()
        wifi = (base / 'esp32_mquickjs_wifi.c').read_text()
        code = BOUNDARIES
        code += ''.join(extract(config, name) for name in (
            'esp32_mquickjs_wifi_ssid_text', 'esp32_mquickjs_wifi_set_ssid_properties'))
        code += ''.join(extract(wifi, name) for name in (
            'wifi_negotiated_phy_name', 'wifi_link_snapshot_valid', 'wifi_set_link_properties',
            'esp32_mquickjs_wifi_make_connect_result'))
        with tempfile.TemporaryDirectory() as tmp:
            run([str(build(tmp, code, MAIN))])
