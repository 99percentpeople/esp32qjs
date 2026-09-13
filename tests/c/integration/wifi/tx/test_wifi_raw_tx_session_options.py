"""Deferred real MQuickJS Session option capture and moving-GC/OOM checks."""
from tests.support.fixtures import fixture_text
import json
import tempfile
import unittest
from tests.c.integration.wifi.tx.test_wifi_tx_rate import rate_code

from tests.c.integration.wifi.monitor.test_wifi_rx_target import ROOT, INTERNAL, unit
from tests.support.wireless_vm_fixture import CORE, build, extract, run


class WiFiRawTxSessionOptions(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.temp.cleanup)
        source = (ROOT / 'components/esp32_mquickjs/src/modules/wifi_raw_tx/esp32_mquickjs_wifi_raw_tx_public_session.c').read_text()
        symbols = json.loads((ROOT / 'docs/idf-wifi-api-inventory.json').read_text())['variants']['esp32c5/representative']['symbols']
        types = {key.split('::')[-1]: value['declaration'] for key, value in symbols.items()}
        extra = '#define CONFIG_ESP32_MQUICKJS_FEATURE_WIFI 1\n#define CONFIG_ESP_WIFI_SOFTAP_SUPPORT 1\n#define CONFIG_SOC_WIFI_SUPPORT_5G 1\ntypedef int esp_err_t;\n'
        for name in ['wifi_interface_t', 'wifi_phy_rate_t', 'wifi_tx_status_t', 'wifi_tx_info_t', 'esp_80211_tx_info_t']:
            extra += types[name] + ';\n'
        extra += rate_code('esp32c5/representative', ('wifi_interface_t', 'wifi_phy_rate_t', 'wifi_tx_status_t', 'wifi_tx_info_t', 'esp_80211_tx_info_t'))
        for name in ['wifi_rx', 'wifi_raw_tx_validate', 'wifi_raw_tx_snapshot', 'wifi_raw_tx_broker', 'wifi_raw_tx_queue', 'wifi_raw_tx_session']:
            extra += unit(INTERNAL / ('esp32_mquickjs_' + name + '.h'))
        extra += 'typedef esp32_mquickjs_wifi_raw_tx_session_options_t session_options_t;\n'
        extra += unit(CORE / 'esp32_mquickjs_options.c')
        radio = (ROOT / 'components/esp32_mquickjs/src/modules/wifi_radio/esp32_mquickjs_wifi_radio.c').read_text()
        extra += extract(radio, 'esp32_mquickjs_wifi_radio_5ghz_channel_bit')
        driver = (ROOT / 'components/esp32_mquickjs/src/modules/wifi_driver/esp32_mquickjs_wifi_driver.c').read_text()
        extra += extract(driver, 'esp32_mquickjs_wifi_tx_rate_capture')
        for name in ['capture_timeout', 'capture_send_options', 'capture_open_options']:
            extra += extract(source, name)
        cls.binary = build(cls.temp.name, extra, MAIN)

    def test_complete_capture_defaults_enums_and_queue_bounds(self):
        cases = [
            ('undefined', True), ('({})', True),
            ('({interface:"access-point",channel:36,sequenceControl:"application",validation:"basic",timeoutMs:60000,queue:{capacityPackets:128,overflow:"drop-oldest-batch"}})', True),
            ('({channel:"current",queue:{capacityPackets:1},timeoutMs:1})', True),
            ('({rate:{phy:"11g",rate:"6m"}})', True),
            ('({rate:{phy:"ht20",rate:"6m"}})', False),
            ('({rate:{phy:"11g",rate:"6m",dcm:0}})', False),
            ('({interface:"access-point",rate:{phy:"11g",rate:"6m"}})', True),
            ('({get rate(){throw new Error("sentinel");}})', False),
            ('({rate:{get phy(){throw new Error("sentinel");},rate:"6m"}})', False),
            ('({queue:{capacityPackets:0}})', False), ('({queue:{capacityPackets:129}})', False),
            ('({queue:{capacityPackets:1.5}})', False), ('({queue:null})', False),
            ('({queue:{overflow:"drop-oldest"}})', False), ('({queue:{overflow:"reject-newest\\u0000"}})', False),
            ('({channel:"current\\u0000"})', False), ('({channel:15})', False),
            ('({channel:178})', False), ('({channel:1.5})', False),
            ('({interface:"station\\u0000"})', False), ('({sequenceControl:"driver\\u0000"})', False),
            ('({timeoutMs:0})', False), ('({timeoutMs:60001})', False),
            ('({timeoutMs:"1000"})', False), ('({unknown:true})', False), ('null', False), ('[]', False),
            ('({get channel(){throw new Error("sentinel");}})', False),
            ('({queue:{get capacityPackets(){throw new Error("sentinel");}}})', False),
            ('(function(){var n=0,m=0;return {get channel(){if(++n!==1)throw new Error("twice");return 6;},get queue(){if(++m!==1)throw new Error("twice");return {capacityPackets:4};}};})()', True),
        ]
        for expression, valid in cases:
            with self.subTest(expression=expression):
                run([str(self.binary), 'open', expression, str(int(valid)), 'sentinel' if 'sentinel' in expression else ''])

    def test_send_timeout_is_the_only_per_send_option(self):
        for expression, valid in [('undefined', True), ('({timeoutMs:1})', True),
                                  ('({timeoutMs:60000})', True), ('({timeoutMs:1.5})', False),
                                  ('({channel:6})', False), ('({timeoutMs:0})', False),
                                  ('({get timeoutMs(){throw new Error("sentinel");}})', False)]:
            with self.subTest(expression=expression):
                run([str(self.binary), 'send', expression, str(int(valid)), 'sentinel' if 'sentinel' in expression else ''])


MAIN = fixture_text('wifi/tx/test_wifi_raw_tx_session_options/main.inc')
