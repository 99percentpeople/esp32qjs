"""Deferred production TWT capture/validation with real MQuickJS getters and GC.

Property lookup is an injected GC/failure boundary; it roots its by-value input
like the VM API. No SDK operation/session or alternate request state machine.
"""
from tests.support.fixtures import fixture_text
from pathlib import Path
import re
import tempfile
import unittest
from tests.c.integration.wifi.monitor.test_wifi_rx_target import unit, INTERNAL
from tests.c.integration.wifi.config.test_wifi_config_controls import structure
from tests.support.wireless_vm_fixture import ROOT, CORE, build, run


class WiFiTwtOptions(unittest.TestCase):
    def test_native_timing_bounds_getter_gc_and_capture_failure(self):
        header = Path('/home/zach/esp/esp-idf/components/esp_wifi/include/esp_wifi_he_types.h').read_text()
        extra = '#define CONFIG_ESP32_MQUICKJS_FEATURE_WIFI 1\n#define CONFIG_SOC_WIFI_HE_SUPPORT 1\n'
        extra += re.search(r'typedef enum \{[^}]*\} wifi_twt_setup_cmds_t;', header).group(0)
        extra += structure(header, 'wifi_twt_setup_config_t')
        extra += '\ntypedef wifi_twt_setup_config_t wifi_itwt_setup_config_t;\n'
        extra += structure(header, 'wifi_btwt_setup_config_t')
        extra += unit(INTERNAL / 'esp32_mquickjs_wifi_twt_options.h')
        extra += unit(CORE / 'esp32_mquickjs_options.c')
        extra += BOUNDARY
        extra += unit(ROOT / 'components/esp32_mquickjs/src/modules/wifi_twt/esp32_mquickjs_wifi_twt_options.c')
        cases = [
            ('i', '{}', True),
            ('i', 'undefined', False), ('i', 'null', False), ('i', '[]', False),
            ('i', '{command:"demand",flowId:7,connectionId:32767,trigger:false,announced:false,wakeDurationUnit:"1024us",minimumWakeDuration:255,wakeIntervalExponent:20,wakeIntervalMantissa:32768,responseTimeoutMs:65535,timeoutMs:60000}', True),
            ('i', '{connectionId:0}', True), ('i', '{connectionId:32768}', False),
            ('i', '{flowId:8}', False), ('i', '{flowId:-1}', False),
            ('i', '{flowId:1.5}', False), ('i', '{flowId:"1"}', False),
            ('i', '{flowId:NaN}', False), ('i', '{timeoutMs:Infinity}', False),
            ('i', '{responseTimeoutMs:99}', False), ('i', '{responseTimeoutMs:100}', True),
            ('i', '{responseTimeoutMs:65536}', False), ('i', '{timeoutMs:0}', False),
            ('i', '{timeoutMs:60001}', False), ('i', '{timeoutMs:1,responseTimeoutMs:65535}', True),
            ('i', '{minimumWakeDuration:1,wakeIntervalMantissa:10256,wakeIntervalExponent:0}', True),
            ('i', '{minimumWakeDuration:1,wakeIntervalMantissa:10255,wakeIntervalExponent:0}', False),
            ('i', '{wakeDurationUnit:"1024us",minimumWakeDuration:1,wakeIntervalMantissa:11024,wakeIntervalExponent:0}', True),
            ('i', '{wakeIntervalMantissa:16,wakeIntervalExponent:31}', True),
            ('i', '{wakeIntervalMantissa:17,wakeIntervalExponent:31}', False),
            ('i', '{wakeIntervalMantissa:65535,wakeIntervalExponent:31}', False),
            ('i', '{wakeIntervalExponent:32}', False), ('i', '{minimumWakeDuration:256}', False),
            ('i', '{trigger:1}', False), ('i', '{announced:"true"}', False),
            ('i', '{command:"accept"}', False), ('i', '{command:"request\\x00x"}', False),
            ('i', '{wakeDurationUnit:"tu"}', False), ('i', '{implicit:true}', False),
            ('i', '{get command(){gc();return "suggest";},get connectionId(){gc();return 31;},get trigger(){gc();return false;}}', True),
            ('i', '{get flowId(){throw 12345;}}', False),
            ('i', '(function(){var reads=0;return {get connectionId(){gc();if(++reads!==1)throw 12345;return 9;}};})()', True),
            ('b', '{broadcastId:1}', True), ('b', '{broadcastId:31,responseTimeoutMs:1,timeoutMs:1}', True),
            ('b', '{}', False), ('b', '{broadcastId:0}', False), ('b', '{broadcastId:32}', False),
            ('b', '{broadcastId:255}', False), ('b', '{broadcastId:1,flowId:0}', False),
            ('b', '{broadcastId:1,responseTimeoutMs:0}', False),
            ('b', '{broadcastId:1,responseTimeoutMs:65536}', False),
            ('b', '{command:"demand",get broadcastId(){gc();return 12;}}', True),
            ('b', '{get broadcastId(){throw 12345;}}', False),
        ]
        with tempfile.TemporaryDirectory() as tmp:
            binary = build(tmp, extra, MAIN)
            for kind, expression, valid in cases:
                run([str(binary), kind, expression, str(int(valid))])


BOUNDARY = fixture_text('wifi/twt/test_wifi_twt_options/boundary.inc')
MAIN = fixture_text('wifi/twt/test_wifi_twt_options/main.inc')
