"""Production raw capture + ByteView/VM, with shared policy-parser boundaries."""
from tests.support.fixtures import fixture_text
import tempfile
import unittest
from tests.support.wireless_vm_fixture import ROOT, CORE, build, extract, run

BOUNDARIES = fixture_text('wifi/driver/test_wifi_driver_capture/boundaries.inc')

MAIN = fixture_text('wifi/driver/test_wifi_driver_capture/main.inc')


class WiFiDriverCapture(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.temp.cleanup)
        options = (CORE / 'esp32_mquickjs_options.c').read_text().replace(
            '#include "esp32_mquickjs_options.h"',
            (ROOT / 'components/esp32_mquickjs/internal/esp32_mquickjs_options.h').read_text().replace(
                '#include "esp32_mquickjs_types.h"', ''))
        source = (ROOT / 'components/esp32_mquickjs/src/modules/wifi/esp32_mquickjs_wifi_config.c').read_text()
        radio = (ROOT / 'components/esp32_mquickjs/src/modules/wifi_radio/esp32_mquickjs_wifi_radio.c').read_text()
        body = options + BOUNDARIES
        body += (ROOT / 'components/esp32_mquickjs/internal/esp32_mquickjs_wifi_config_fields.h').read_text()
        body += extract((CORE / 'esp32_mquickjs_wireless_core.c').read_text(), 'esp32_mquickjs_wireless_secure_zero')
        body += extract(radio, 'esp32_mquickjs_wifi_radio_5ghz_channel_bit')
        body += ''.join(extract(source, n) for n in ('wifi_capture_config_ssid', 'wifi_driver_config_unsupported',
            'esp32_mquickjs_wifi_parse_driver_config_for_operation', 'esp32_mquickjs_wifi_parse_driver_config'))
        cls.binary = build(cls.temp.name, body, MAIN)

    def capture(self, options, ap=False, expected=True, raw=b'A', mode=''):
        run([str(self.binary), '(' + options + ')', str(int(ap)), str(int(expected)), raw.hex(), mode])

    def test_text_arrays_arraylikes_and_tu(self):
        self.capture('{ssid:"A"}')
        self.capture('{ssid:[255,65]}', raw=bytes([255,65]))
        self.capture('{ssid:{length:2,0:255,1:65}}', raw=bytes([255,65]))
        self.capture('{ssid:[255,0,65],beaconIntervalTu:60000,channel:36}', ap=True, raw=bytes([255,0,65]), mode='tu')
        self.capture('{ssid:"A",password:"12345678",pmf:"optional"}', ap=True)
        self.capture('{ssid:"A",password:""}', ap=True)
        self.capture('{ssid:"A",pmf:"required"}', mode='required')
        self.capture('{ssid:"A",pmf:"disabled",disableWpa3CompatibleMode:true}')

    def test_public_driver_operation_uses_the_same_full_capture(self):
        self.capture('{ssid:"A",password:"12345678"}', mode='driver')
        self.capture('{ssid:[255,0,65]}', ap=True, raw=bytes([255,0,65]), mode='driver')
        self.capture('{ssid:"A",unknown:1}', expected=False, mode='driver')

    def test_byteview_read_lease_is_released_on_all_exits(self):
        self.capture('{}', raw=b'A', mode='view')
        self.capture('{}', ap=True, raw=bytes([255,0,65]), mode='view')
        self.capture('{}', expected=False, mode='closed-view')
        self.capture('{}', expected=False, mode='long-view')
        self.capture('{}', expected=False, raw=b'', mode='view')

    def test_binary_bounds_invalid_keys_and_failed_policy_clear_output(self):
        for options in ['{ssid:[]}', '{ssid:{length:4294967295}}', '{ssid:[0]}', '{ssid:[256]}',
                        '{ssid:[1.5]}', '{ssid:["65"]}', '{ssid:[1,undefined,2]}', '{ssid:"A",timeoutMs:10}',
                        '{ssid:"A",unexpected:true}', '{ssid:"A",pmf:1}']:
            self.capture(options, expected=False)
        for options in ['{ssid:"A",beaconIntervalTu:101}', '{ssid:"A",beaconIntervalMs:100}',
                        '{ssid:"A",channel:35}']:
            self.capture(options, ap=True, expected=False)
        self.capture('{ssid:"A",pmf:"optional"}', expected=False, mode='required')
        self.capture('{ssid:"A"}', expected=False, mode='policy-fail')
        self.capture('{ssid:"A"}', ap=True, expected=False, mode='validate-fail')
