"""Deferred real VM capture/conversion, Nth allocation and moving-GC coverage."""
from tests.support.fixtures import fixture_text
import json
import re
import tempfile
import unittest
from tests.c.integration.wifi.tx.test_wifi_tx_rate import rate_code

from tests.c.integration.wifi.config.test_wifi_config_controls import HEADER, structure
from tests.c.integration.wifi.monitor.test_wifi_rx_target import ROOT, INTERNAL, COMMON, unit
from tests.support.wireless_vm_fixture import CORE, build, extract, run

RAW = ROOT / 'components/esp32_mquickjs/src/modules/wifi_raw_tx'


class WiFiRawTxCaptureGC(unittest.TestCase):
    extended = False

    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.temp.cleanup)
        source = (RAW / 'esp32_mquickjs_wifi_raw_tx.c').read_text()
        symbols = json.loads((ROOT / 'docs/idf-wifi-api-inventory.json').read_text())['variants']['esp32c5/representative']['symbols']
        types = {key.split('::')[-1]: value['declaration'] for key, value in symbols.items()}
        extra = '#define ESP32_MQUICKJS_RAW_TX_EXTENDED_MANAGEMENT '+str(int(cls.extended))+'\n'
        extra += fixture_text('wifi/tx/test_wifi_raw_tx_capture_gc/setupclass-extra.inc')
        for name in ['wifi_interface_t', 'wifi_phy_rate_t', 'wifi_tx_status_t', 'wifi_tx_info_t', 'esp_80211_tx_info_t']:
            extra += types[name] + ';\n'
        extra += rate_code('esp32c5/representative', ('wifi_interface_t', 'wifi_phy_rate_t', 'wifi_tx_status_t', 'wifi_tx_info_t', 'esp_80211_tx_info_t')).replace('#define CONFIG_SOC_WIFI_SUPPORT_5G 1', '#define CONFIG_SOC_WIFI_SUPPORT_5G 0')
        for name in ['wifi_rx', 'wifi_raw_tx_validate', 'wifi_raw_tx_snapshot', 'wifi_raw_tx_broker', 'wifi_raw_tx_lane']:
            extra += unit(INTERNAL / ('esp32_mquickjs_' + name + '.h'))
        extra += structure((INTERNAL / 'esp32_mquickjs_wifi_raw_tx_session.h').read_text(), 'esp32_mquickjs_wifi_raw_tx_sessions_status_t')
        extra += structure((INTERNAL / 'esp32_mquickjs_wifi_raw_tx_periodic_job.h').read_text(), 'esp32_mquickjs_wifi_raw_tx_periodic_jobs_status_t')
        extra += unit(COMMON / 'esp32_mquickjs_wifi_rx.c')
        extra += unit(COMMON / 'esp32_mquickjs_wifi_frame_type.c')
        extra += unit(RAW / 'esp32_mquickjs_wifi_raw_tx_validate.c')
        extra += re.search(r'typedef enum \{[^}]*\} esp32_mquickjs_wifi_radio_client_t;', HEADER.read_text()).group(0)
        extra += structure(HEADER.read_text(), 'esp32_mquickjs_wifi_radio_lease_t')
        extra += structure(source, 'raw_tx_options_t')
        start = source.index('struct esp32_mquickjs_future_driver_state {')
        extra += 'typedef struct esp32_mquickjs_future_driver_state esp32_mquickjs_future_driver_state_t;\n'
        extra += source[start:source.index('\n};', start) + 3]
        extra += unit(CORE / 'esp32_mquickjs_options.c')
        extra += BOUNDARIES
        extra += unit(INTERNAL / 'esp32_mquickjs_js_macros.h')
        for name in ['raw_tx_capture_options', 'esp32_mquickjs_wifi_raw_tx_capture_bytes', 'raw_tx_capture_bytes', 'raw_tx_capture',
                     'raw_tx_frame_control', 'esp32_mquickjs_wifi_raw_tx_result_to_js', 'raw_tx_finish', 'js_wifi_raw_tx_capabilities', 'esp32_mquickjs_wifi_raw_tx_status']:
            extra += extract(source, name)
        cls.binary = build(cls.temp.name, extra, MAIN)

    def test_capture_bounds_single_getters_byteview_close_exceptions_and_gc(self):
        packet = '(function(){var a=[];for(var i=0;i<24;i++)a.push(i===0?128:0);return a;})()'
        cases = [
            ('({frame:view,options:{validation:"basic"}})', 0),
            ('({frame:view,options:{validation:"strict"}})', 0),
            ('({frame:view})', 1),
            ('({frame:' + packet + ',options:{timeoutMs:1}})', 1),
            ('({frame:view,options:{interface:"access-point",sequenceControl:"application",channel:14,timeoutMs:2147483647}})', 1),
            ('({frame:view,options:{channel:"current"}})', 1),
            ('({frame:view,options:{timeoutMs:0}})', 0),
            ('({frame:view,options:{timeoutMs:2147483648}})', 0),
            ('({frame:view,options:{timeoutMs:1.5}})', 0),
            ('({frame:view,options:{channel:36}})', 0),
            ('({frame:view,options:{interface:"station\\u0000"}})', 0),
            ('({frame:view,options:{extra:true}})', 0),
            ('({frame:view,options:null})', 0),
            ('({frame:view,options:[]})', 0),
            ('({frame:{length:4294967295}})', 0),
            ('({frame:{length:23}})', 0),
            ('({frame:{length:1501}})', 0),
            ('({frame:{length:24,0:256}})', 0),
            ('({frame:{length:24,0:1.5}})', 0),
            ('({frame:view,options:{get channel(){view.close();return "current";}}})', 0),
            ('({frame:{get length(){throw new Error("sentinel");}}})', 0),
            ('({frame:{length:24,get 0(){throw new Error("sentinel");}}})', 0),
            ('({frame:view,options:{get channel(){throw new Error("sentinel");}}})', 0),
            ('(function(){var n=0;var a=' + packet + ';var f={get length(){if(++n!==1)throw new Error("twice");return 24;}};for(var i=0;i<24;i++)f[i]=a[i];return {frame:f};})()', 1),
            ('(function(){var n=0;return {frame:view,options:{get channel(){if(++n!==1)throw new Error("twice");return "current";}}};})()', 1),
        ]
        for expression, expected in cases:
            with self.subTest(expression=expression):
                run([str(self.binary), 'capture', expression, str(expected),
                     'sentinel' if 'sentinel' in expression else ''])

    def test_result_and_capabilities_construction_allocation_failure_and_gc(self):
        run([str(self.binary), 'finish', '', '1', ''])
        run([str(self.binary), 'capabilities', '', '1', ''])
        run([str(self.binary), 'status', '', '1', ''])
        run([str(self.binary), 'terminated-status', '', '1', ''])


BOUNDARIES = fixture_text('wifi/tx/test_wifi_raw_tx_capture_gc/boundaries.inc')

MAIN = fixture_text('wifi/tx/test_wifi_raw_tx_capture_gc/main.inc')


class WiFiRawTxExtendedCaptureGC(WiFiRawTxCaptureGC):
    extended = True
