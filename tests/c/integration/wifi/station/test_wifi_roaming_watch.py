"""Deferred scoped watch options/GC and production subscriber filtering.

Uses the shared production parser, SDK descriptors, mask and capture path.
Queue/lock/time boundaries come from watch_values. Does not stand in for full
EventQueue factory/close scheduling or RF evidence. Do not run before Wi-Fi gate.
"""
from tests.support.fixtures import fixture_text
import tempfile
import unittest
from tests.c.integration.wifi.monitor.test_wifi_rx_target import unit
from tests.c.integration.wifi.config.test_wifi_watch_values import WATCH, watch_code
from tests.support.wireless_vm_fixture import CORE, build, extract, run


class WiFiRoamingWatch(unittest.TestCase):
    def test_scope_options_gc_and_sdk_build_gate(self):
        source = WATCH.read_text()
        for rrm in (0, 1):
            code = watch_code()
            code += f'\n#undef CONFIG_ESP_WIFI_RRM_SUPPORT\n#define CONFIG_ESP_WIFI_RRM_SUPPORT {rrm}\n'
            code += '#define ESP32_MQUICKJS_WIFI_MAX_WATCH_CAPACITY 64\n'
            code += unit(CORE / 'esp32_mquickjs_options.c')
            code += extract(source, 'wifi_roaming_watch_mask')
            code += extract(source, 'wifi_watch_options')
            cases = [('undefined', 1), ('({})', 1), ('null', 0), ('[]', 0),
                     ('({events:"all"})', 1), ('({events:"all\\x00"})', 0),
                     ('({events:[]})', 0), ('({events:[undefined]})', 0),
                     ('({events:["WIFI_EVENT_STA_STOP"]})', 1),
                     ('({events:["WIFI_EVENT_STA_NEIGHBOR_REP"]})', rrm),
                     ('({events:["WIFI_EVENT_AP_START"]})', 0),
                     ('({events:["WIFI_EVENT_STA_STOP\\x00"]})', 0),
                     ('({events:["WIFI_EVENT_STA_STOP","WIFI_EVENT_STA_STOP"]})', 0),
                     ('({capacity:1})', 1), ('({capacity:64})', 1),
                     ('({capacity:0})', 0), ('({capacity:65})', 0),
                     ('({capacity:1.5})', 0), ('({capacity:"16"})', 0),
                     ('({overflow:"drop-newest"})', 1), ('({overflow:"drop-oldest"})', 0),
                     ('({includeRawEventData:false})', 0), ('({extra:1})', 0),
                     ('({get capacity(){gc();return 8;},get events(){gc();return ["WIFI_EVENT_STA_STOP"];}})', 1),
                     ('({get events(){gc();throw new Error("sentinel");}})', 0),
                     # The bundled VM does not invoke this indexed getter;
                     # reading the existing array slot produces undefined. Actual
                     # options getters and array reads above still run with GC.
                     ('(function(){var a=[undefined];Object.defineProperty(a,"0",{get:function(){gc();return "WIFI_EVENT_STA_STOP";}});return {events:a};})()', 0),
                     ('(function(){Object.defineProperty(Object.prototype,"includeRawEventData",{get:function(){throw new Error("must not read");}});return {};})()', 1)]
            with tempfile.TemporaryDirectory() as tmp:
                binary = build(tmp, code, MAIN)
                for expression, valid in cases:
                    with self.subTest(rrm=rrm, expression=expression):
                        run([str(binary), expression, str(valid), 'scoped'])
                for expression in ['({includeRawEventData:true,events:"all"})',
                                   '({events:["WIFI_EVENT_AP_START"]})']:
                    run([str(binary), expression, '1', 'generic'])


MAIN = fixture_text('wifi/station/test_wifi_roaming_watch/main.inc')
